using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

// Read only disposable, local-page QA sessions. Chromium's command framing and
// navigation payload are defined by components/sessions/core/session_command.cc,
// session_service_commands.cc and base_session_service_commands.cc in
// https://github.com/chromium/chromium. Encrypted or changed formats fail
// explicitly; a text search through old session files is not restoration proof.
public static class ChromiumSessionFixture
{
    private sealed class Tab
    {
        public int Window;
        public int Selected = -1;
        public readonly SortedDictionary<int, string> Navigations = new SortedDictionary<int, string>();
    }

    public static string[] CurrentUrls(string path)
    {
        byte[] bytes;
        using (var file = new FileStream(path, FileMode.Open, FileAccess.Read,
                    FileShare.ReadWrite | FileShare.Delete))
        {
            if (file.Length < 8 || file.Length > 16 * 1024 * 1024)
                throw new InvalidDataException("Invalid fixture session size.");
            bytes = new byte[(int)file.Length];
            int offset = 0;
            while (offset < bytes.Length)
            {
                int read = file.Read(bytes, offset, bytes.Length - offset);
                if (read == 0) throw new EndOfStreamException();
                offset += read;
            }
        }
        if (BitConverter.ToInt32(bytes, 0) != 0x53534E53 || BitConverter.ToInt32(bytes, 4) != 3)
            throw new InvalidDataException("Fixture session must use Chromium's supported cleartext version 3.");
        var tabs = new Dictionary<int, Tab>();
        var closedWindows = new HashSet<int>();
        bool initialStateComplete = false;
        Func<int, Tab> get = id => { Tab tab; if (!tabs.TryGetValue(id, out tab)) tabs[id] = tab = new Tab(); return tab; };
        for (int position = 8; position < bytes.Length;)
        {
            if (bytes.Length - position < 3) throw new InvalidDataException("Incomplete fixture session command.");
            int length = BitConverter.ToUInt16(bytes, position);
            if (length < 1 || length > bytes.Length - position - 2) throw new InvalidDataException("Invalid fixture session command length.");
            int command = bytes[position + 2], payload = position + 3, size = length - 1;
            if (command == 255)
            {
                if (size != 0) throw new InvalidDataException("Invalid fixture snapshot marker.");
                initialStateComplete = true;
            }
            else if (command == 0)
            {
                if (size != 8) throw new InvalidDataException("Invalid tab/window fixture command.");
                get(BitConverter.ToInt32(bytes, payload + 4)).Window = BitConverter.ToInt32(bytes, payload);
            }
            else if (command == 7)
            {
                if (size != 8) throw new InvalidDataException("Invalid selected-navigation fixture command.");
                get(BitConverter.ToInt32(bytes, payload)).Selected = BitConverter.ToInt32(bytes, payload + 4);
            }
            else if (command == 16)
            {
                if (size != 16) throw new InvalidDataException("Invalid closed-tab fixture command.");
                tabs.Remove(BitConverter.ToInt32(bytes, payload));
            }
            else if (command == 17)
            {
                if (size != 16) throw new InvalidDataException("Invalid closed-window fixture command.");
                closedWindows.Add(BitConverter.ToInt32(bytes, payload));
            }
            else if (command == 5 || command == 11 || command == 24)
                throw new InvalidDataException("Navigation pruning is outside the controlled one-navigation-per-tab QA fixture.");
            else if (command == 6)
            {
                // Pickle header, tab ID, navigation index, UTF-8 URL length.
                if (size < 16) throw new InvalidDataException("Invalid navigation fixture command.");
                int pickleSize = BitConverter.ToInt32(bytes, payload);
                int urlSize = BitConverter.ToInt32(bytes, payload + 12);
                if (pickleSize != size - 4 || urlSize < 0 || urlSize > size - 16)
                    throw new InvalidDataException("Invalid navigation fixture payload.");
                string url = new UTF8Encoding(false, true).GetString(bytes, payload + 16, urlSize);
                get(BitConverter.ToInt32(bytes, payload + 4)).Navigations[BitConverter.ToInt32(bytes, payload + 8)] = url;
            }
            position += 2 + length;
        }
        if (!initialStateComplete) throw new InvalidDataException("Fixture session initial snapshot is not complete.");
        var currentTabs = tabs.Values.Where(tab => tab.Window != 0 && !closedWindows.Contains(tab.Window) && tab.Navigations.Count != 0).ToArray();
        if (currentTabs.Any(tab => tab.Selected < 0 || !tab.Navigations.ContainsKey(tab.Selected)))
            throw new InvalidDataException("Fixture tab has no proven selected navigation.");
        return currentTabs.Select(tab => tab.Navigations[tab.Selected]).ToArray();
    }
}
