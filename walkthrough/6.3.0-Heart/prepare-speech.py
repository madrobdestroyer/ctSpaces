"""Prepare this project's narration and measured recording slots."""
import json
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parent
story=json.loads((ROOT/'storyboard.json').read_text(encoding='utf-8'))
job=json.loads((ROOT/'heart-speech-job.json').read_text(encoding='utf-8-sig'))
if sys.argv[1:]==['--prepare']:
    job['cues']=[{'id':c['id'],'text':c['text'],'path':str(ROOT/'media-work'/(c['id']+'.wav'))} for v in story['videos'] for c in v['chapters']]
    (ROOT/'heart-speech-job.json').write_text(json.dumps(job,indent=2),encoding='utf-8')
elif sys.argv[1:]==['--time']:
    receipt=json.loads((ROOT/'heart-speech-receipt.json').read_text())
    cues={c['id']:c for c in receipt['cues']}
    for video in story['videos']:
        elapsed=0
        for chapter in video['chapters']:
            chapter['start']=elapsed
            chapter['audioSeconds']=cues[chapter['id']]['durationSeconds']
            # Full motion continues while any short narration tail finishes.
            chapter['duration']=max({'app-find':28,'app-browser':27,'notes-write':28,'notes-template':35,'notes-snippet':35,'notes-code':32,'notes-library':25,'notes-actions':35}.get(chapter['id'],21),chapter['audioSeconds']+2.0)
            chapter['wav']=cues[chapter['id']]['path']
            elapsed+=chapter['duration']
        video['duration']=elapsed
    (ROOT/'timing.json').write_text(json.dumps(story,indent=2),encoding='utf-8')
else:
    raise SystemExit('Use --prepare or --time')
