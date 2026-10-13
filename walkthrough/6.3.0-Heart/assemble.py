"""Join measured Heart cues with real native recordings and build a chapter player."""
import hashlib
import html
import json
from pathlib import Path
import subprocess
import wave

ROOT = Path(__file__).resolve().parent
OUT = ROOT / 'output'
OUT.mkdir(exist_ok=True)
story = json.loads((ROOT / 'timing.json').read_text(encoding='utf-8-sig'))

def stamp(seconds):
    ms = round(seconds * 1000)
    return f'{ms//3600000:02}:{ms//60000%60:02}:{ms//1000%60:02}.{ms%1000:03}'

def media(mode, job):
    path = OUT / (job['id'] + '-' + mode.lower() + '-job.json')
    path.write_text(json.dumps(job, indent=2), encoding='utf-8')
    # Match Atlas's existing WinRT export route without changing execution policy.
    script = str(ROOT / 'media-tools.ps1').replace("'", "''")
    command = f"& ([scriptblock]::Create([IO.File]::ReadAllText('{script}'))) -Mode {mode} -Job '{str(path)}'"
    result = subprocess.run([r'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe',
                             '-NoProfile', '-Command', command], text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (OUT / (job['id'] + '-' + mode.lower() + '.log')).write_text(result.stdout, encoding='utf-8')
    print(result.stdout, flush=True)
    if result.returncode:
        raise RuntimeError(f'{mode} failed: {job["id"]}')

players = []
for video in story['videos']:
    key = video['id']
    receipt = json.loads((ROOT / (key + '-recording.json')).read_text(encoding='utf-8-sig'))
    capture = json.loads((Path(receipt['run']) / 'capture-receipt.json').read_text(encoding='utf-8-sig'))
    assert receipt['completed'] and capture['changedFrames'] > 100
    assert receipt['duration'] >= video['duration'] - .3
    assert [c['id'] for c in receipt['chapters']] == [c['id'] for c in video['chapters']]
    for actual, planned in zip(receipt['chapters'], video['chapters']):
        assert abs(actual['start'] - planned['start']) < .6, (actual, planned)
    audio = OUT / (key + '-Heart.wav')
    subtitles = []
    with wave.open(str(audio), 'wb') as joined:
        joined.setnchannels(1)
        joined.setsampwidth(2)
        joined.setframerate(24000)
        written = 0
        for chapter in video['chapters']:
            start = round(chapter['start'] * 24000)
            assert written == start
            with wave.open(chapter['wav'], 'rb') as cue:
                assert (cue.getnchannels(), cue.getsampwidth(), cue.getframerate()) == (1, 2, 24000)
                count = cue.getnframes()
                assert abs(count/24000 - chapter['audioSeconds']) < .001
                joined.writeframes(cue.readframes(count))
            end = round((chapter['start'] + chapter['duration']) * 24000)
            joined.writeframes(b'\x00\x00' * (end - start - count))
            written = end
            words = chapter['text'].split()
            for first in range(0, len(words), 13):
                last = min(first + 13, len(words))
                subtitles.append({'start': chapter['start'] + first/len(words)*chapter['audioSeconds'],
                                  'end': chapter['start'] + last/len(words)*chapter['audioSeconds'],
                                  'text': ' '.join(words[first:last])})
    vtt = 'WEBVTT\n\n' + '\n\n'.join(f"{stamp(c['start'])} --> {stamp(c['end'])}\n{c['text']}" for c in subtitles) + '\n'
    (OUT / (key + '.vtt')).write_text(vtt, encoding='utf-8')
    srt = '\n\n'.join(f"{i+1}\n{stamp(c['start']).replace('.', ',')} --> {stamp(c['end']).replace('.', ',')}\n{c['text']}" for i,c in enumerate(subtitles)) + '\n'
    (OUT / (key + '.srt')).write_text(srt, encoding='utf-8')
    output = OUT / (key + '-Walkthrough.mp4')
    media('Export', dict(id=key, parts=[dict(video=receipt['raw'], duration=video['duration'])],
                         videos=[], audio=str(audio), output=str(output), width=1440, height=900,
                         fps=20, bitrate=6000000))
    inspection = OUT / (key + '-inspection.json')
    media('Inspect', dict(id=key, output=str(output), inspection=str(inspection),
                         thumbnails=[dict(time=c['start']+min(c['duration']-1,15),
                                          path=str(OUT/(c['id']+'.png')),width=1440,height=900)
                                     for c in video['chapters']]))
    info = json.loads(inspection.read_text(encoding='utf-8-sig'))
    assert (info['width'], info['height']) == (1440,900)
    assert abs(info['durationSeconds'] - video['duration']) < .4
    assert info['audioSubtype'] == 'AAC' and info['channels'] >= 1
    players.append(dict(id=key,title=video['title'],src=output.name,duration=video['duration'],
                        chapters=[{field:c[field] for field in ('id','title','text','start','duration')}
                                  for c in video['chapters']],captions=subtitles))

template = (ROOT / 'player.html').read_text(encoding='utf-8')
encoded = json.dumps(players).replace('<', '\\u003c')
(OUT / 'index.html').write_text(template.replace('/*VIDEO_DATA*/ []', encoded), encoding='utf-8')
baseline = json.loads((ROOT / 'read-only-resource-baseline.json').read_text(encoding='utf-8-sig'))
for resource in baseline:
    assert hashlib.sha256(Path(resource['Path']).read_bytes()).hexdigest().upper() == resource['Hash']
files = {p.name:dict(bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest())
         for p in OUT.iterdir() if p.suffix in ('.mp4','.html','.vtt','.srt')}
(OUT / 'delivery-receipt.json').write_text(json.dumps(dict(files=files,sharedResourcesUnchanged=True,
    voice='af_heart',language='en-us',speed=1.0,source='real allowed native application windows',
    videos=players),indent=2),encoding='utf-8')
print('Validated two narrated videos, subtitles, chapter player and read-only resources.',flush=True)
