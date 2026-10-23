#!/usr/bin/env python3
"""Bounded screenshot diagnostics via the user's existing local vision model; never pulls."""
import base64, json, sys, urllib.request
from pathlib import Path
endpoint='http://127.0.0.1:11434'
with urllib.request.urlopen(endpoint+'/api/tags',timeout=3) as response:
    models=json.loads(response.read(131072))['models']
if not any(model['name']=='qwen3-vl:8b' for model in models):raise SystemExit('qwen3-vl:8b unavailable; no download requested')
data=Path(sys.argv[1]).read_bytes()
if len(data)>2*1024*1024:raise SystemExit('Image too large')
prompt=sys.argv[2] if len(sys.argv)>2 else 'Check whether window text and controls are readable. Return only a concise list of specific issues in English.'
body=json.dumps({'model':'qwen3-vl:8b','stream':False,'think':False,'messages':[{'role':'user','content':prompt,'images':[base64.b64encode(data).decode()]}],'options':{'num_ctx':4096,'num_predict':350,'temperature':0},'keep_alive':'2m'}).encode()
request=urllib.request.Request(endpoint+'/api/chat',data=body,headers={'Content-Type':'application/json'})
with urllib.request.urlopen(request,timeout=180) as response:
    result=response.read(262145)
if len(result)>262144:raise SystemExit('Response too large')
print(json.loads(result)['message']['content'][:2500])
