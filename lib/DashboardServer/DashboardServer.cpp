#include "DashboardServer.h"
#include "SessionAuth.h"
#include "SettingsStore.h"
#include "ConfigManager.h"
#include "BuzzerController.h"
#include "DoorSensor.h"
#include "CameraAgent.h"
#include "FaceRecognizer.h"
#include "config.h"
#include "states.h"
#include <ArduinoJson.h>

// ── HTML templates ────────────────────────────────────────────────────────────

static const char DASHBOARD_HTML[] =
  "<!DOCTYPE html><html><head>"
  "<meta charset='utf-8'><title>Agent 1 - %AGENT%</title>"
  "<style>"
  "body{font-family:sans-serif;max-width:820px;margin:20px auto;padding:20px}"
  ".card{background:#f5f5f5;border-radius:8px;padding:16px;margin:12px 0}"
  ".st{font-size:1.4em;font-weight:bold}.lbl{color:#666;font-size:.85em}"
  "a{color:#0070f3}nav{margin-bottom:16px}.hidden{display:none}"
  ".row{display:flex;gap:16px;flex-wrap:wrap}.col{flex:1;min-width:260px}"
  "#cam{width:100%;border-radius:6px;background:#222;min-height:180px;display:block}"
  ".bdg{display:inline-block;padding:2px 10px;border-radius:10px;font-size:.85em;font-weight:bold}"
  ".gr{background:#d4edda;color:#155724}.rd{background:#f8d7da;color:#721c24}"
  ".yl{background:#fff3cd;color:#856404}.no{background:#e2e3e5;color:#555}"
  ".kp{background:#cce5ff;color:#004085}.up{background:#ffd8a8;color:#7d4e00}"
  ".frl{list-style:none;padding:0;margin:8px 0}"
  ".frl li{padding:4px 0;border-bottom:1px solid #ddd;font-size:.9em}"
  "input[type=text]{width:100%;padding:7px;margin:6px 0;box-sizing:border-box;"
  "border:1px solid #ccc;border-radius:4px}"
  "button{padding:7px 14px;background:#0070f3;color:#fff;border:none;"
  "border-radius:4px;cursor:pointer;margin-right:4px}"
  "button:disabled{opacity:.5}.red{background:#dc3545}"
  "</style></head><body>"
  "<h2>Agent 1 &mdash; %AGENT%</h2>"
  "<nav>"
  "<a href='/settings'>Settings</a> | "
  "<a href='/log/door'>Door Log</a> | "
  "<a href='/log/face'>Face Log</a> | "
  "<a href='/log/alert'>Alert Log</a> | "
  "<a href='/analytics'>Analytics</a> | "
  "<a href='/logout'>Logout</a>"
  "</nav>"
  "<div class='row'>"
  "<div class='col'>"
  "<div class='card'><div class='lbl'>Alert Level</div><span class='bdg no' id='al'>&mdash;</span></div>"
  "<div class='card'><div class='lbl'>Door</div><div id='door'>&mdash;</div></div>"
  "<div class='card'><div class='lbl'>Agent 2</div><div id='a2'>&mdash;</div></div>"
  "<div class='card'><div class='lbl'>Last Known User</div><div id='lku'>&mdash;</div></div>"
  "<div class='card'><div class='lbl'>Uptime</div><div id='up'>&mdash;</div></div>"
  "<div class='card'><div class='lbl'>Hall Sensor</div><div id='hall'>&mdash;</div></div>"
  "</div>"
  "<div class='col'>"
  "<div class='card'><div class='lbl'>Camera Preview</div>"
  "<img id='cam' alt='stream'>"
  "<div style='margin-top:8px;display:flex;gap:16px;flex-wrap:wrap'>"
  "<div><div class='lbl'>Now</div><span id='raw' class='bdg no'>&mdash;</span></div>"
  "<div><div class='lbl'>Vote</div><span id='badge' class='bdg no'>&mdash;</span></div>"
  "</div></div>"
  "</div>"
  "</div>"
  "<div class='card'>"
  "<div class='lbl'>Face Recognition (<span id='fc'>0</span>/%MAX% enrolled)</div>"
  "<ul class='frl' id='fl'><li style='color:#999'>No faces enrolled</li></ul>"
  "<input type='text' id='fn' placeholder='Enter name to enroll' maxlength='16'>"
  "<button onclick='enr()' id='eb'>Enroll Face</button>"
  "<button onclick='clr()' class='red'>Clear All</button>"
  "<div id='msg' class='lbl' style='margin-top:4px'></div>"
  "</div>"
  "<div id='cv' style='display:none'>%CSRF%</div>"
  "<script>"
  "document.getElementById('cam').src='http://'+location.hostname+':81/stream';"
  "function fmt(ms){var s=Math.floor(ms/1000);"
  "return Math.floor(s/3600)+'h '+Math.floor(s%3600/60)+'m '+s%60+'s';}"
  "function poll(){"
  "fetch('/api/status').then(r=>r.json()).then(d=>{"
  "var al=document.getElementById('al');"
  "if(d.alert_level==='ALERT_RED'){al.className='bdg rd';al.textContent='RED'+(d.alarm_active?' ⚠️ ALARM':' ●');}"
  "else if(d.alert_level==='ALERT_YELLOW'){al.className='bdg yl';al.textContent='YELLOW ●';}"
  "else{al.className='bdg gr';al.textContent='GREEN ●';}"
  "document.getElementById('door').textContent=d.door_state||'?';"
  "var a2=document.getElementById('a2');"
  "a2.textContent=d.agent2_online?('Online · '+(d.presence_state||'?')):'Offline';"
  "a2.style.color=d.agent2_online?'#155724':'#721c24';"
  "document.getElementById('lku').textContent=d.last_known_user||'—';"
  "document.getElementById('up').textContent=fmt(d.uptime||0);"
  "document.getElementById('hall').textContent='raw: '+d.hall_raw+' / zone: '+d.hall_lower+'-'+d.hall_upper;"
  "if(d.face_count!==undefined)document.getElementById('fc').textContent=d.face_count;"
  "var r=document.getElementById('raw');"
  "if(d.face_result==='KNOWN'){r.className='bdg gr';"
  "r.textContent=(d.face_name||'Known')+(d.face_sim>0?' · '+d.face_sim.toFixed(3):'');}"
  "else if(d.face_result==='UNKNOWN'){r.className='bdg rd';"
  "r.textContent='Unknown'+(d.face_tex>0?' · tex:'+d.face_tex.toFixed(1):'');}"
  "else if(d.face_result==='DETECTED'){r.className='bdg no';r.textContent='Detected';}"
  "else{r.className='bdg no';r.textContent='—';}"
  "var b=document.getElementById('badge'),fv=d.face_voter_state;"
  "if(fv==='known_confirmed'){b.className='bdg gr';"
  "b.textContent=d.face_voter_confirmed_name||d.face_name||'Known';}"
  "else if(fv==='unknown_pending'){b.className='bdg up';"
  "b.textContent='Unknown… '+d.face_voter_unknown_elapsed_s+'s/'+d.face_voter_unknown_window_s+'s · '+d.face_voter_unknown_hits+' hits';}"
  "else if(fv==='known_pending'){b.className='bdg kp';"
  "b.textContent='Known? '+(d.face_name?d.face_name+' ':'')+d.face_voter_known_count+'/'+d.face_voter_known_min+' hits';}"
  "else if(d.face_result==='KNOWN'){b.className='bdg gr';b.textContent=d.face_name||'Known';}"
  "else if(d.face_result==='UNKNOWN'){b.className='bdg rd';b.textContent='Unknown';}"
  "else if(d.face_result==='DETECTED'){b.className='bdg no';b.textContent='Detected';}"
  "else{b.className='bdg no';b.textContent='—';}"
  "}).catch(()=>{});"
  "fetch('/api/face/list').then(r=>r.json()).then(d=>{"
  "var ul=document.getElementById('fl');ul.innerHTML='';"
  "if(d.faces&&d.faces.length){"
  "d.faces.forEach(function(n,i){"
  "var li=document.createElement('li');"
  "li.textContent=(i+1)+'. '+(n||'(unnamed)');ul.appendChild(li);});}"
  "else ul.innerHTML='<li style=\"color:#999\">No faces enrolled</li>';"
  "}).catch(()=>{});}"
  "function enr(){"
  "var n=document.getElementById('fn').value.trim();"
  "if(!n){document.getElementById('msg').textContent='Please enter a name.';return;}"
  "var c=document.getElementById('cv').textContent;"
  "document.getElementById('eb').disabled=true;"
  "document.getElementById('msg').textContent='Stand in front of camera...';"
  "fetch('/api/face/enroll',{method:'POST',"
  "headers:{'Content-Type':'application/x-www-form-urlencoded'},"
  "body:'csrf='+encodeURIComponent(c)+'&name='+encodeURIComponent(n)})"
  ".then(r=>r.json()).then(function(d){"
  "document.getElementById('msg').textContent="
  "d.error?('Error: '+d.error):'Scheduled: \"'+n+'\" will be enrolled on next detection';"
  "document.getElementById('eb').disabled=false;"
  "document.getElementById('fn').value='';}).catch(function(){document.getElementById('eb').disabled=false;});}"
  "function clr(){"
  "if(!confirm('Clear all enrolled faces?'))return;"
  "var c=document.getElementById('cv').textContent;"
  "fetch('/api/face/clear',{method:'POST',"
  "headers:{'Content-Type':'application/x-www-form-urlencoded'},"
  "body:'csrf='+encodeURIComponent(c)})"
  ".then(r=>r.json()).then(function(){"
  "document.getElementById('msg').textContent='All faces cleared.';"
  "document.getElementById('fl').innerHTML='<li style=\"color:#999\">No faces enrolled</li>';}).catch(()=>{});}"
  "poll();setInterval(poll,3000);"
  "</script></body></html>";

static const char SETTINGS_HTML[] =
  "<!DOCTYPE html><html><head>"
  "<meta charset='utf-8'><title>Settings - Agent 1</title>"
  "<style>body{font-family:sans-serif;max-width:520px;margin:20px auto;padding:20px}"
  "fieldset{border:1px solid #ddd;border-radius:6px;padding:12px;margin:12px 0}"
  "legend{font-weight:bold}"
  "input{width:100%;padding:8px;margin:6px 0;box-sizing:border-box}"
  "button{padding:10px 20px;background:#0070f3;color:#fff;border:none;cursor:pointer}"
  "a{color:#0070f3}.ok{color:green;font-size:.9em}.err{color:red;font-size:.9em}"
  "</style></head><body>"
  "<h2>Settings</h2><a href='/dashboard'>&larr; Dashboard</a>"
  "%MSG%"
  "<form method='POST' action='/settings/save'>"
  "<input type='hidden' name='csrf' value='%CSRF%'>"
  "<fieldset><legend>Change Password</legend>"
  "<label>New Password (8-64 chars)<br>"
  "<input type='password' name='newpw' minlength='8' maxlength='64'></label>"
  "<label>Confirm<br>"
  "<input type='password' name='confirmpw' maxlength='64'></label>"
  "</fieldset>"
  "<fieldset><legend>Discord Webhook</legend>"
  "<label>URL<br>"
  "<input name='discord_url' maxlength='256' "
  "placeholder='https://discord.com/api/webhooks/...' value='%DISCORD_URL%'></label>"
  "</fieldset>"
  "<fieldset><legend>MQTT Broker</legend>"
  "<label>Broker IP / Hostname<br>"
  "<input name='mqtt_broker' maxlength='63' placeholder='192.168.x.x' value='%MQTT_BROKER%'></label>"
  "<label>Port<br>"
  "<input type='number' name='mqtt_port' min='1' max='65535' value='%MQTT_PORT%'></label>"
  "</fieldset>"
  "<fieldset><legend>Hall Sensor Bounds</legend>"
  "<label>Lower Bound (0-4095)<br>"
  "<input type='number' name='hall_lower' min='0' max='4095' value='%HALL_LOWER%'></label>"
  "<label style='margin-top:8px;display:block'>Upper Bound (0-4095)<br>"
  "<input type='number' name='hall_upper' min='0' max='4095' value='%HALL_UPPER%'></label>"
  "<p style='font-size:.8em;color:#666'>Current raw: %HALL_RAW% &mdash; "
  "door OPEN when raw is between lower and upper bounds.</p>"
  "</fieldset>"
  "<fieldset><legend>Buzzer</legend>"
  "<label>Frequency (200-8000 Hz)<br>"
  "<input type='number' name='buzzer_freq' min='200' max='8000' value='%BUZZER_FREQ%' id='bf'></label>"
  "<label>Alert Duration (10-300 s)<br>"
  "<input type='number' name='buzzer_dur_s' min='10' max='300' value='%BUZZER_DUR_S%'></label>"
  "<button type='button' onclick='tstBz()'>Test</button>"
  "<p style='font-size:.8em;color:#666'>Alarm auto-cancels after duration (buzzer + LED both stop).</p>"
  "</fieldset>"
  "<button>Save</button>"
  "</form>"
  "<script>function tstBz(){"
  "var f=document.getElementById('bf').value;"
  "var c=document.querySelector('[name=csrf]').value;"
  "var fd=new FormData();fd.append('freq',f);fd.append('csrf',c);"
  "fetch('/api/buzzer/test',{method:'POST',body:fd}).then(function(r){"
  "var b=document.getElementById('bf');"
  "b.style.background=r.ok?'#d4edda':'#f8d7da';"
  "setTimeout(function(){b.style.background='';},800);});}"
  "</script>"
  "</body></html>";

static const char PWCHANGE_HTML[] =
  "<!DOCTYPE html><html><head>"
  "<meta charset='utf-8'><title>Set Password - Agent 1</title>"
  "<style>body{font-family:sans-serif;max-width:400px;margin:60px auto;padding:20px}"
  "input{width:100%;padding:8px;margin:6px 0;box-sizing:border-box}"
  "button{width:100%;padding:10px;background:#0070f3;color:#fff;border:none;cursor:pointer}"
  ".err{color:red;font-size:.9em}</style></head><body>"
  "<h2>Set Admin Password</h2>"
  "<p>Please set a new password before continuing.</p>"
  "%ERR%"
  "<form method='POST' action='/password/save'>"
  "<input type='hidden' name='csrf' value='%CSRF%'>"
  "<label>New Password (8-64 chars)<br>"
  "<input type='password' name='newpw' minlength='8' maxlength='64' required></label>"
  "<label>Confirm<br>"
  "<input type='password' name='confirmpw' maxlength='64' required></label>"
  "<button>Set Password</button>"
  "</form></body></html>";

static const char LOG_HTML[] =
  "<!DOCTYPE html><html><head>"
  "<meta charset='utf-8'><title>%TITLE% - Agent 1</title>"
  "<style>body{font-family:sans-serif;max-width:900px;margin:20px auto;padding:20px}"
  "table{width:100%;border-collapse:collapse;font-size:.9em}"
  "th,td{padding:8px;text-align:left;border-bottom:1px solid #ddd}"
  "th{background:#f5f5f5}a{color:#0070f3}"
  ".ctrl{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin:8px 0}"
  "select,input[type=text]{padding:6px;border:1px solid #ccc;border-radius:4px}"
  "button{padding:6px 12px;background:#0070f3;color:#fff;border:none;border-radius:4px;cursor:pointer}"
  "button:disabled{opacity:.5}"
  "</style></head><body>"
  "<h2>%TITLE%</h2>"
  "<nav><a href='/dashboard'>&larr; Dashboard</a> | <a href='/analytics'>Analytics</a></nav>"
  "<div class='ctrl'>"
  "<select id='mo' onchange='onMoChange()'><option value=''>Recent (RAM)</option></select>"
  "<input type='text' id='fi' placeholder='filter...' oninput='applyFilter()'>"
  "<span id='pi'></span>"
  "</div>"
  "<div class='ctrl'>"
  "<button onclick='prev()' id='pb' disabled>&larr; Prev</button>"
  "<button onclick='next()' id='nb' disabled>Next &rarr;</button>"
  "</div>"
  "<div id='tl' style='margin:8px 0'></div>"
  "<div id='tbl'><p>Loading...</p></div>"
  "<script>"
  "function esc(v){return String(v===null||v===undefined?'':v)"
  ".replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/\"/g,'&quot;');}"
  "var pg=1,tot=0,allRows=[];"
  "function renderRows(rows){"
  "if(!rows.length){document.getElementById('tbl').innerHTML='<p>No entries.</p>';return;}"
  "var keys=Object.keys(rows[0]);"
  "var h='<table><tr>'+keys.map(k=>'<th>'+esc(k)+'</th>').join('')+'</tr>';"
  "rows.forEach(function(row){h+='<tr>'+keys.map(k=>'<td>'+esc(row[k])+'</td>').join('')+'</tr>';});"
  "document.getElementById('tbl').innerHTML=h+'</table>';}"
  "var LOG_TYPE='%LOG_TYPE%';"
  "function getColor(r){"
  "if(LOG_TYPE==='door')return r.door_state==='DOOR_OPEN'?'#dc3545':'#28a745';"
  "if(LOG_TYPE==='face')return r.vote_result==='KNOWN_CONFIRMED'?'#28a745':r.vote_result==='UNKNOWN_CONFIRMED'?'#dc3545':'#aaa';"
  "return r.alert_level==='ALERT_RED'?'#dc3545':'#ffc107';}"
  "function getY(r){"
  "if(LOG_TYPE==='door')return r.door_state==='DOOR_OPEN'?15:27;"
  "if(LOG_TYPE==='face')return r.vote_result==='KNOWN_CONFIRMED'?15:r.vote_result==='UNKNOWN_CONFIRMED'?27:39;"
  "return r.alert_level==='ALERT_RED'?15:27;}"
  "function drawTimeline(rows){"
  "var now=Date.now(),ws=now-604800000;"
  "var s='<svg width=\"100%\" height=\"70\" viewBox=\"0 0 720 70\">';"
  "s+='<line x1=\"0\" y1=\"50\" x2=\"720\" y2=\"50\" stroke=\"#ddd\"/>';"
  "var lr=LOG_TYPE==='door'?[[15,'#dc3545','OPEN'],[27,'#28a745','CLOSE']]:"
  "LOG_TYPE==='face'?[[15,'#28a745','KWN'],[27,'#dc3545','UNK'],[39,'#aaa','?']]:"
  "[[15,'#dc3545','RED'],[27,'#ffc107','YLW']];"
  "lr.forEach(function(l){s+='<text x=\"718\" y=\"'+(l[0]+4)+'\" font-size=\"7\" fill=\"'+l[1]+'\" text-anchor=\"end\">'+l[2]+'</text>';});"
  "for(var i=0;i<=7;i++){var x=(i/7*720).toFixed(0),d=new Date(ws+i*86400000);"
  "var lbl=i===7?'today':('0'+(d.getMonth()+1)).slice(-2)+'/'+('0'+d.getDate()).slice(-2);"
  "s+='<line x1=\"'+x+'\" y1=\"50\" x2=\"'+x+'\" y2=\"55\" stroke=\"#bbb\"/>';"
  "s+='<text x=\"'+x+'\" y=\"67\" font-size=\"8\" fill=\"'+(i===7?'#333':'#888')+'\" text-anchor=\"middle\">'+lbl+'</text>';}"
  "rows.forEach(function(r){"
  "var ts=r.timestamp||'';if(!ts)return;"
  "var evtMs=new Date(ts).getTime();"
  "if(isNaN(evtMs)||evtMs<ws||evtMs>now+300000)return;"
  "var x=((evtMs-ws)/604800000*720).toFixed(1),c=getColor(r),y=getY(r),lbl=ts.substring(11,16);"
  "s+='<circle cx=\"'+x+'\" cy=\"'+y+'\" r=\"4\" fill=\"'+c+'\" opacity=\"0.85\"><title>'+lbl+'</title></circle>';});"
  "document.getElementById('tl').innerHTML=s+'</svg>';}"
  "function applyFilter(){"
  "var fi=document.getElementById('fi').value.toLowerCase();"
  "var rows=fi?allRows.filter(r=>JSON.stringify(r).toLowerCase().includes(fi)):allRows;"
  "renderRows(rows);drawTimeline(rows);}"
  "function onMoChange(){pg=1;loadData();}"
  "function prev(){if(pg>1){pg--;loadData();}}"
  "function next(){if(pg*20<tot){pg++;loadData();}}"
  "function loadData(){"
  "var mo=document.getElementById('mo').value;"
  "var url=mo?('%API_PAGED%?page='+pg+'&per_page=20&month='+mo):'%API_RING%';"
  "fetch(url).then(r=>r.json()).then(function(d){"
  "if(Array.isArray(d)){allRows=d;tot=d.length;pg=1;}"
  "else{allRows=d.data||[];tot=d.total||0;}"
  "var pages=Math.ceil(tot/20)||1;"
  "document.getElementById('pi').textContent='Page '+pg+'/'+pages+' ('+tot+' entries)';"
  "document.getElementById('pb').disabled=(pg<=1);"
  "document.getElementById('nb').disabled=(pg>=pages);"
  "applyFilter();"
  "}).catch(function(){document.getElementById('tbl').innerHTML='<p>Failed to load.</p>';});}"
  "fetch('/api/log/months').then(r=>r.json()).then(function(months){"
  "var sel=document.getElementById('mo');"
  "months.forEach(function(m){"
  "var o=document.createElement('option');o.value=m;"
  "var y=Math.floor(m/100),mn=m%100;"
  "o.text=y+'-'+(mn<10?'0':'')+mn;sel.appendChild(o);});"
  "if(months.length)sel.value=months[0];"
  "loadData();"
  "}).catch(function(){loadData();});"
  "</script></body></html>";

static const char ANALYTICS_HTML[] =
  "<!DOCTYPE html><html><head>"
  "<meta charset='utf-8'><title>Analytics - Agent 1</title>"
  "<style>body{font-family:sans-serif;max-width:900px;margin:20px auto;padding:20px}"
  ".card{background:#f5f5f5;border-radius:8px;padding:16px;margin:12px 0}"
  ".row{display:flex;gap:16px;flex-wrap:wrap}.col{flex:1;min-width:200px}"
  "a{color:#0070f3}.num{font-size:2em;font-weight:bold;color:#0070f3}"
  ".numd{font-size:2em;font-weight:bold;color:#dc3545}.sub{font-size:.9em;color:#666}"
  "select{padding:6px;border:1px solid #ccc;border-radius:4px;margin-bottom:8px}"
  "</style></head><body>"
  "<h2>Analytics</h2>"
  "<nav><a href='/dashboard'>&larr; Dashboard</a> | "
  "<a href='/log/door'>Door</a> | <a href='/log/face'>Face</a> | <a href='/log/alert'>Alert</a></nav>"
  "<select id='mo' onchange='load()'><option value='0'>This Month</option></select>"
  "<div class='row'>"
  "<div class='col card'><div class='sub'>Door Events Today</div>"
  "<div class='num' id='dt'>-</div>"
  "<div class='sub'>Week: <span id='dw'>-</span> &bull; Month: <span id='dm'>-</span></div>"
  "<div class='sub'>Open: <span id='do'>-</span> &bull; Closed: <span id='dco'>-</span></div>"
  "<div class='sub'>Night:<span id='dnt'>-</span> Day:<span id='ddy'>-</span> Eve:<span id='dev'>-</span></div>"
  "<div class='sub'>Avg open:<span id='dao'>-</span> Max:<span id='dmo'>-</span>"
  " &bull; <span id='duc' style='color:#dc3545'></span></div></div>"
  "<div class='col card'><div class='sub'>Face Recognitions Today</div>"
  "<div class='num' id='ft'>-</div>"
  "<div class='sub'>Week: <span id='fw'>-</span> &bull; Month: <span id='fm'>-</span></div>"
  "<div class='sub'>Known%: <span id='fk'>-</span> &bull; Unknown: <span id='fuc'>-</span></div>"
  "<div class='sub'>Night: <span id='fnt'>-</span></div></div>"
  "<div class='col card'><div class='sub'>Alerts Today</div>"
  "<div class='numd' id='at'>-</div>"
  "<div class='sub'>Week: <span id='aw'>-</span> &bull; Month: <span id='am'>-</span></div>"
  "<div class='sub'>Last: <span id='al'>none</span></div>"
  "<div class='sub'>Triggered: <span id='atr'>-</span> &bull; Discord OK: <span id='adk'>-</span></div></div>"
  "</div>"
  "<div class='card'><div class='sub'>Door Events (last 7 days)</div><div id='dch'></div></div>"
  "<div class='card'><div class='sub'>Face Events (last 7 days)</div><div id='fch'></div></div>"
  "<div class='card'><div class='sub'>Peak Hours (door + face)</div><div id='phch'></div></div>"
  "<div class='row'>"
  "<div class='col card'><div class='sub'>Active Users (month)</div><div id='lb'>-</div></div>"
  "<div class='col card'><div class='sub'>SPIFFS Storage</div>"
  "<div id='sti' class='sub'>-</div>"
  "<div style='background:#e0e0e0;border-radius:3px;height:6px;margin-top:4px'>"
  "<div id='spb' style='background:#0070f3;height:6px;border-radius:3px;width:0'></div></div></div>"
  "</div>"
  "<script>"
  "function drawBar(series,cid,lbl){"
  "var n=series[0].data.length,tots=[];"
  "for(var i=0;i<n;i++){var t=0;series.forEach(function(s){t+=s.data[i]||0;});tots.push(t);}"
  "var m=Math.max.apply(null,tots.concat([1]));"
  "var days=lbl||['M','T','W','T','F','S','S'];"
  "var sv='<svg width=\"100%\" height=\"80\" viewBox=\"0 0 210 80\">';"
  "for(var i=0;i<n;i++){"
  "var x=i*30+5,bot=70;"
  "series.forEach(function(sr){var v=sr.data[i]||0,h=Math.round(v/m*60);"
  "if(h>0){bot-=h;sv+='<rect x=\"'+x+'\" y=\"'+bot+'\" width=\"22\" height=\"'+h+'\" fill=\"'+sr.color+'\"/>';}});"
  "sv+='<text x=\"'+(x+11)+'\" y=\"78\" font-size=\"8\" text-anchor=\"middle\">'+days[i]+'</text>';"
  "if(tots[i])sv+='<text x=\"'+(x+11)+'\" y=\"'+(70-Math.round(tots[i]/m*60)-2)+'\" font-size=\"7\" text-anchor=\"middle\">'+tots[i]+'</text>';}"
  "document.getElementById(cid).innerHTML=sv+'</svg>';}"
  "function load(){"
  "var mo=document.getElementById('mo').value||'0';"
  "fetch('/api/log/stats?month='+mo).then(r=>r.json()).then(function(d){"
  "['dch','fch','phch'].forEach(function(id){document.getElementById(id).innerHTML='';});"
  "var dr=d.door||{},fr=d.face||{},ar=d.alert||{};"
  "document.getElementById('dt').textContent=dr.today||0;"
  "document.getElementById('dw').textContent=dr.week||0;"
  "document.getElementById('dm').textContent=dr.month_total||0;"
  "document.getElementById('do').textContent=dr.open_count||0;"
  "document.getElementById('dco').textContent=dr.close_count||0;"
  "document.getElementById('ft').textContent=fr.today||0;"
  "document.getElementById('fw').textContent=fr.week||0;"
  "document.getElementById('fm').textContent=fr.month_total||0;"
  "document.getElementById('fk').textContent=(fr.known_pct||0)+'%';"
  "document.getElementById('at').textContent=ar.today||0;"
  "document.getElementById('aw').textContent=ar.week||0;"
  "document.getElementById('am').textContent=ar.month_total||0;"
  "document.getElementById('al').textContent=ar.last_at?(ar.last_at.substring(0,16)):'none';"
  "document.getElementById('dnt').textContent=dr.night_count||0;"
  "document.getElementById('ddy').textContent=dr.day_count||0;"
  "document.getElementById('dev').textContent=dr.evening_count||0;"
  "document.getElementById('dao').textContent=dr.avg_open_secs?dr.avg_open_secs+'s':'-';"
  "document.getElementById('dmo').textContent=dr.max_open_secs?dr.max_open_secs+'s':'-';"
  "document.getElementById('duc').textContent=dr.unclosed?'⚠ Door open!':'';"
  "document.getElementById('fnt').textContent=fr.night_count||0;"
  "document.getElementById('fuc').textContent=fr.unknown_count||0;"
  "document.getElementById('atr').textContent=ar.trigger_count||0;"
  "document.getElementById('adk').textContent=(ar.discord_ok||0)+'/'+(ar.red_count||0);"
  "var ds=[];"
  "if(dr.daily_open)ds.push({data:dr.daily_open,color:'#dc3545'});"
  "if(dr.daily_close)ds.push({data:dr.daily_close,color:'#28a745'});"
  "if(ds.length)drawBar(ds,'dch',d.week_labels);"
  "var uc=['#0070f3','#f59e0b','#10b981','#8b5cf6','#ec4899','#14b8a6','#e60023'];"
  "var fs=[];"
  "if(d.leaderboard)d.leaderboard.forEach(function(u,i){if(u.daily)fs.push({data:u.daily,color:uc[i%7]});});"
  "if(fr.daily_unknown)fs.push({data:fr.daily_unknown,color:'#aaa'});"
  "if(fs.length)drawBar(fs,'fch',d.week_labels);"
  "if(d.peak_hours)drawBar([{data:d.peak_hours,color:'#0070f3'}],'phch',['00','04','08','12','16','20']);"
  "if(d.leaderboard&&d.leaderboard.length){"
  "document.getElementById('lb').innerHTML=d.leaderboard.map(function(u){"
  "return '<b>'+esc(u.name)+'</b>&nbsp;'+u.count;}).join(' &bull; ');}"
  "if(d.storage_total_kb){"
  "var pct=Math.min(100,Math.round(d.storage_used_kb/d.storage_total_kb*100));"
  "document.getElementById('sti').textContent=d.storage_used_kb+'KB / '+d.storage_total_kb+'KB ('+pct+'%)';"
  "document.getElementById('spb').style.width=pct+'%';}"
  "}).catch(function(){});}"
  "fetch('/api/log/months').then(r=>r.json()).then(function(months){"
  "var sel=document.getElementById('mo');"
  "months.forEach(function(m){"
  "var o=document.createElement('option');o.value=m;"
  "var y=Math.floor(m/100),mn=m%100;"
  "o.text=y+'-'+(mn<10?'0':'')+mn;sel.appendChild(o);});"
  "}).catch(function(){});"
  "load();"
  "</script></body></html>";

// ── Module-level state ────────────────────────────────────────────────────────

static SecurityStateMachine* _sm         = nullptr;
static const char*           _agentLabel = nullptr;
static FaceVoter*            _faceVoter  = nullptr;
static LogManager*           _logManager = nullptr;

// ── Helpers ───────────────────────────────────────────────────────────────────

static String htmlAttrEscape(const String& s) {
  String out;
  out.reserve(s.length() + 16);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if      (c == '&')  out += "&amp;";
    else if (c == '"')  out += "&quot;";
    else if (c == '\'') out += "&#39;";
    else if (c == '<')  out += "&lt;";
    else if (c == '>')  out += "&gt;";
    else                out += c;
  }
  return out;
}

static bool requireAuth(WebServer& server) {
  if (!SessionAuth::isAuthorized(server)) {
    server.sendHeader("Location", "/login");
    server.send(302, "text/plain", "");
    return false;
  }
  return true;
}

static bool requireAuthAndChangedPassword(WebServer& server) {
  if (!SessionAuth::isAuthorized(server)) {
    server.sendHeader("Location", "/login");
    server.send(302, "text/plain", "");
    return false;
  }
  if (SettingsStore::hasDefaultPassword()) {
    server.sendHeader("Location", "/password/change");
    server.send(302, "text/plain", "");
    return false;
  }
  return true;
}

// ── Public API ────────────────────────────────────────────────────────────────

void DashboardServer::begin(WebServer& server,
                             SecurityStateMachine& sm,
                             const char* agentLabel,
                             FaceVoter* faceVoter,
                             LogManager* logManager) {
  _sm         = &sm;
  _agentLabel = agentLabel;
  _faceVoter  = faceVoter;
  _logManager = logManager;

  // Root → dashboard
  server.on("/", HTTP_GET, [&server]() {
    server.sendHeader("Location", "/dashboard");
    server.send(302, "text/plain", "");
  });

  server.on("/favicon.ico", HTTP_GET, [&server]() {
    server.send(204, "text/plain", "");
  });

  // ── Dashboard page ──────────────────────────────────────────────────────────
  server.on("/dashboard", HTTP_GET, [&server]() {
    if (!requireAuthAndChangedPassword(server)) return;
    String page = DASHBOARD_HTML;
    page.replace("%AGENT%", _agentLabel ? _agentLabel : "");
    page.replace("%CSRF%",  SessionAuth::getCsrfToken());
    page.replace("%MAX%",   String(FaceRecognizer::MAX_FACES));
    server.send(200, "text/html", page);
  });

  // ── Status API ──────────────────────────────────────────────────────────────
  server.on("/api/status", HTTP_GET, [&server]() {
    if (!requireAuthAndChangedPassword(server)) return;

    JsonDocument doc;
    doc["alert_level"]     = alertLevelToString(_sm->getAlertLevel());
    doc["door_state"]      = doorStateToString(_sm->getDoorState());
    doc["agent2_online"]   = _sm->isAgent2Online();
    doc["alarm_active"]    = _sm->isAlarmActive();
    doc["last_known_user"] = _sm->getLastKnownUser();
    doc["presence_state"]  = "";   // updated by AgentComm callback; reflected via sm
    doc["uptime"]          = millis();
    doc["hall_raw"]   = DoorSensor::getRaw();
    doc["hall_lower"] = SettingsStore::getHallLowerBound();
    doc["hall_upper"] = SettingsStore::getHallUpperBound();
    doc["face_count"]      = FaceRecognizer::count();
    doc["face_max"]        = FaceRecognizer::MAX_FACES;

    // Recognition result (only within FACE_RECENT_MS)
    FaceResult fr = FaceResult::NONE;
    if (CameraAgent::isInitialized()) {
      unsigned long sinceDetect = millis() - CameraAgent::lastDetectedMs();
      if (sinceDetect < FACE_RECENT_MS) {
        fr = CameraAgent::lastRawResult();
      }
    }
    doc["face_result"] = (fr == FaceResult::KNOWN)    ? "KNOWN"    :
                         (fr == FaceResult::UNKNOWN)   ? "UNKNOWN"  :
                         (fr == FaceResult::DETECTED)  ? "DETECTED" : "NONE";
    {
      const char* fn = FaceRecognizer::getLastMatchName();
      doc["face_name"] = fn ? fn : "";
    }
    doc["face_sim"] = FaceRecognizer::getLastSim();
    doc["face_tex"] = FaceRecognizer::getLastTex();

    // FaceVoter state
    if (_faceVoter) {
      FaceVoterStatus fvs = _faceVoter->getStatus(millis());
      const char* voterState;
      if      (fvs.knownConfirmed)   voterState = "known_confirmed";
      else if (!fvs.active)          voterState = "idle";
      else if (fvs.unknownHits > 0)  voterState = "unknown_pending";
      else if (fvs.knownCount > 0)   voterState = "known_pending";
      else                           voterState = "active";
      doc["face_voter_state"]             = voterState;
      doc["face_voter_confirmed_name"]    = fvs.confirmedName;
      doc["face_voter_known_count"]       = fvs.knownCount;
      doc["face_voter_known_min"]         = fvs.knownMin;
      doc["face_voter_unknown_hits"]      = fvs.unknownHits;
      doc["face_voter_unknown_elapsed_s"] = (int)(fvs.unknownElapsedMs / 1000UL);
      doc["face_voter_unknown_window_s"]  = (int)(fvs.unknownWindowMs  / 1000UL);
    }

    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
  });

  // ── Log API ─────────────────────────────────────────────────────────────────
  server.on("/api/log/door", HTTP_GET, [&server]() {
    if (!requireAuthAndChangedPassword(server)) return;
    server.send(200, "application/json", _logManager ? _logManager->getDoorLogJson() : "[]");
  });

  server.on("/api/log/face", HTTP_GET, [&server]() {
    if (!requireAuthAndChangedPassword(server)) return;
    server.send(200, "application/json", _logManager ? _logManager->getFaceLogJson() : "[]");
  });

  server.on("/api/log/alert", HTTP_GET, [&server]() {
    if (!requireAuthAndChangedPassword(server)) return;
    server.send(200, "application/json", _logManager ? _logManager->getAlertLogJson() : "[]");
  });

  // ── Log pages ───────────────────────────────────────────────────────────────
  server.on("/log/door", HTTP_GET, [&server]() {
    if (!requireAuthAndChangedPassword(server)) return;
    String page = LOG_HTML;
    page.replace("%TITLE%",     "Door Log");
    page.replace("%LOG_TYPE%",  "door");
    page.replace("%API_RING%",  "/api/log/door");
    page.replace("%API_PAGED%", "/api/log/door/paged");
    server.send(200, "text/html", page);
  });

  server.on("/log/face", HTTP_GET, [&server]() {
    if (!requireAuthAndChangedPassword(server)) return;
    String page = LOG_HTML;
    page.replace("%TITLE%",     "Face Log");
    page.replace("%LOG_TYPE%",  "face");
    page.replace("%API_RING%",  "/api/log/face");
    page.replace("%API_PAGED%", "/api/log/face/paged");
    server.send(200, "text/html", page);
  });

  server.on("/log/alert", HTTP_GET, [&server]() {
    if (!requireAuthAndChangedPassword(server)) return;
    String page = LOG_HTML;
    page.replace("%TITLE%",     "Alert Log");
    page.replace("%LOG_TYPE%",  "alert");
    page.replace("%API_RING%",  "/api/log/alert");
    page.replace("%API_PAGED%", "/api/log/alert/paged");
    server.send(200, "text/html", page);
  });

  // ── Log paged API ───────────────────────────────────────────────────────────
  server.on("/api/log/door/paged", HTTP_GET, [&server]() {
    if (!requireAuthAndChangedPassword(server)) return;
    uint32_t month   = (uint32_t)server.arg("month").toInt();
    int32_t  rawPage    = (int32_t)server.arg("page").toInt();
    int32_t  rawPerPage = (int32_t)server.arg("per_page").toInt();
    uint16_t page    = (rawPage >= 1 && rawPage <= 9999) ? (uint16_t)rawPage : 1;
    uint16_t perPage = (rawPerPage >= 1 && rawPerPage <= 50) ? (uint16_t)rawPerPage : 20;
    if (month == 0) month = LogManager::getCurrentMonth();
    server.send(200, "application/json",
                LogManager::getDoorLogPagedJson(month, page, perPage));
  });

  server.on("/api/log/face/paged", HTTP_GET, [&server]() {
    if (!requireAuthAndChangedPassword(server)) return;
    uint32_t month   = (uint32_t)server.arg("month").toInt();
    int32_t  rawPage    = (int32_t)server.arg("page").toInt();
    int32_t  rawPerPage = (int32_t)server.arg("per_page").toInt();
    uint16_t page    = (rawPage >= 1 && rawPage <= 9999) ? (uint16_t)rawPage : 1;
    uint16_t perPage = (rawPerPage >= 1 && rawPerPage <= 50) ? (uint16_t)rawPerPage : 20;
    if (month == 0) month = LogManager::getCurrentMonth();
    server.send(200, "application/json",
                LogManager::getFaceLogPagedJson(month, page, perPage));
  });

  server.on("/api/log/alert/paged", HTTP_GET, [&server]() {
    if (!requireAuthAndChangedPassword(server)) return;
    uint32_t month   = (uint32_t)server.arg("month").toInt();
    int32_t  rawPage    = (int32_t)server.arg("page").toInt();
    int32_t  rawPerPage = (int32_t)server.arg("per_page").toInt();
    uint16_t page    = (rawPage >= 1 && rawPage <= 9999) ? (uint16_t)rawPage : 1;
    uint16_t perPage = (rawPerPage >= 1 && rawPerPage <= 50) ? (uint16_t)rawPerPage : 20;
    if (month == 0) month = LogManager::getCurrentMonth();
    server.send(200, "application/json",
                LogManager::getAlertLogPagedJson(month, page, perPage));
  });

  server.on("/api/log/stats", HTTP_GET, [&server]() {
    if (!requireAuthAndChangedPassword(server)) return;
    uint32_t month = (uint32_t)server.arg("month").toInt();
    server.send(200, "application/json", LogManager::getStatsJson(month));
  });

  server.on("/api/log/months", HTTP_GET, [&server]() {
    if (!requireAuthAndChangedPassword(server)) return;
    server.send(200, "application/json", LogManager::getAvailableMonthsJson());
  });

  // ── Analytics page ───────────────────────────────────────────────────────────
  server.on("/analytics", HTTP_GET, [&server]() {
    if (!requireAuthAndChangedPassword(server)) return;
    server.send(200, "text/html", ANALYTICS_HTML);
  });

  // ── Settings page ───────────────────────────────────────────────────────────
  server.on("/settings", HTTP_GET, [&server]() {
    if (!requireAuthAndChangedPassword(server)) return;
    String page = SETTINGS_HTML;
    page.replace("%MSG%",         "");
    page.replace("%CSRF%",        SessionAuth::getCsrfToken());
    page.replace("%DISCORD_URL%", htmlAttrEscape(SettingsStore::getDiscordUrl()));
    page.replace("%MQTT_BROKER%", htmlAttrEscape(ConfigManager::getMqttBroker()));
    page.replace("%MQTT_PORT%",   String(ConfigManager::getMqttPort()));
    page.replace("%HALL_LOWER%",  String(SettingsStore::getHallLowerBound()));
    page.replace("%HALL_UPPER%",  String(SettingsStore::getHallUpperBound()));
    page.replace("%HALL_RAW%",    String(DoorSensor::getRaw()));
    page.replace("%BUZZER_FREQ%", String(ConfigManager::getBuzzerFreq()));
    page.replace("%BUZZER_DUR_S%",String(ConfigManager::getBuzzerDurationMs() / 1000));
    server.send(200, "text/html", page);
  });

  // ── Settings save ───────────────────────────────────────────────────────────
  server.on("/settings/save", HTTP_POST, [&server, &sm]() {
    if (!requireAuthAndChangedPassword(server)) return;
    if (!SessionAuth::verifyCsrf(server.arg("csrf"))) {
      server.send(403, "text/plain", "CSRF validation failed.");
      return;
    }

    String msg;

    // Password change
    String newPw     = server.arg("newpw");
    String confirmPw = server.arg("confirmpw");
    if (newPw.length() > 0) {
      if (newPw != confirmPw) {
        msg += "<p class='err'>Passwords do not match.</p>";
      } else if (!SettingsStore::setDashboardPassword(newPw)) {
        msg += "<p class='err'>Password must be 8-64 characters.</p>";
      } else {
        msg += "<p class='ok'>Password updated.</p>";
      }
    }

    // Discord URL
    if (server.hasArg("discord_url")) {
      String url = server.arg("discord_url");
      if (url.length() == 0) {
        SettingsStore::setDiscordUrl("");
      } else if (!SettingsStore::setDiscordUrl(url)) {
        msg += "<p class='err'>Invalid Discord URL.</p>";
      } else {
        msg += "<p class='ok'>Discord URL saved.</p>";
      }
    }

    // MQTT broker
    if (server.hasArg("mqtt_broker")) {
      String broker = server.arg("mqtt_broker");
      broker.trim();
      String portStr = server.arg("mqtt_port");
      uint16_t port  = (uint16_t)portStr.toInt();
      if (port == 0) port = MQTT_DEFAULT_PORT;
      if (!ConfigManager::save(broker, port)) {
        msg += "<p class='err'>Failed to save MQTT settings.</p>";
      } else {
        msg += "<p class='ok'>MQTT settings saved (restart to apply).</p>";
      }
    }

    // Buzzer settings
    if (server.hasArg("buzzer_freq") && server.hasArg("buzzer_dur_s")) {
      int freq = server.arg("buzzer_freq").toInt();
      int durs = server.arg("buzzer_dur_s").toInt();
      if (freq < 200 || freq > 8000) {
        msg += "<p class='err'>Buzzer frequency must be 200-8000 Hz.</p>";
      } else if (durs < 10 || durs > 300) {
        msg += "<p class='err'>Buzzer duration must be 10-300 s.</p>";
      } else if (!ConfigManager::saveBuzzerSettings((uint32_t)freq, (uint32_t)durs * 1000UL)) {
        msg += "<p class='err'>Failed to save buzzer settings.</p>";
      } else {
        BuzzerController::setFrequency((uint32_t)freq);
        sm.setBuzzerDuration((uint32_t)durs * 1000UL);
        msg += "<p class='ok'>Buzzer settings saved.</p>";
      }
    }

    // Hall sensor bounds
    if (server.hasArg("hall_lower") || server.hasArg("hall_upper")) {
      String loArg = server.arg("hall_lower");
      String hiArg = server.arg("hall_upper");
      bool loNum = loArg.length() > 0;
      bool hiNum = hiArg.length() > 0;
      for (size_t i = 0; i < loArg.length() && loNum; i++) {
        if (!isdigit((unsigned char)loArg[i])) loNum = false;
      }
      for (size_t i = 0; i < hiArg.length() && hiNum; i++) {
        if (!isdigit((unsigned char)hiArg[i])) hiNum = false;
      }
      if (!loNum || !hiNum) {
        msg += "<p class='err'>Hall bounds must be numbers.</p>";
      } else {
        uint16_t lo = (uint16_t)loArg.toInt();
        uint16_t hi = (uint16_t)hiArg.toInt();
        if (!SettingsStore::setHallBounds(lo, hi)) {
          msg += "<p class='err'>Hall bounds invalid (need lower &lt; upper, gap &gt; " +
                 String(2 * HALL_HYSTERESIS) + ").</p>";
        } else {
          DoorSensor::setBounds(lo, hi);
          msg += "<p class='ok'>Hall bounds saved.</p>";
        }
      }
    }

    String page = SETTINGS_HTML;
    page.replace("%MSG%",         msg);
    page.replace("%CSRF%",        SessionAuth::getCsrfToken());
    page.replace("%DISCORD_URL%", htmlAttrEscape(SettingsStore::getDiscordUrl()));
    page.replace("%MQTT_BROKER%", htmlAttrEscape(ConfigManager::getMqttBroker()));
    page.replace("%MQTT_PORT%",   String(ConfigManager::getMqttPort()));
    page.replace("%HALL_LOWER%",  String(SettingsStore::getHallLowerBound()));
    page.replace("%HALL_UPPER%",  String(SettingsStore::getHallUpperBound()));
    page.replace("%HALL_RAW%",    String(DoorSensor::getRaw()));
    page.replace("%BUZZER_FREQ%", String(ConfigManager::getBuzzerFreq()));
    page.replace("%BUZZER_DUR_S%",String(ConfigManager::getBuzzerDurationMs() / 1000));
    server.send(200, "text/html", page);
  });

  // ── Password change pages ───────────────────────────────────────────────────
  server.on("/password/change", HTTP_GET, [&server]() {
    if (!requireAuth(server)) return;
    String page = PWCHANGE_HTML;
    page.replace("%ERR%",  "");
    page.replace("%CSRF%", SessionAuth::getCsrfToken());
    server.send(200, "text/html", page);
  });

  server.on("/password/save", HTTP_POST, [&server]() {
    if (!requireAuth(server)) return;
    if (!SessionAuth::verifyCsrf(server.arg("csrf"))) {
      server.send(403, "text/plain", "CSRF validation failed.");
      return;
    }
    String newPw     = server.arg("newpw");
    String confirmPw = server.arg("confirmpw");
    if (newPw != confirmPw) {
      String page = PWCHANGE_HTML;
      page.replace("%ERR%",  "<p class='err'>Passwords do not match.</p>");
      page.replace("%CSRF%", SessionAuth::getCsrfToken());
      server.send(200, "text/html", page);
      return;
    }
    if (!SettingsStore::setDashboardPassword(newPw)) {
      String page = PWCHANGE_HTML;
      page.replace("%ERR%",  "<p class='err'>Password must be 8-64 characters.</p>");
      page.replace("%CSRF%", SessionAuth::getCsrfToken());
      server.send(200, "text/html", page);
      return;
    }
    server.sendHeader("Location", "/dashboard");
    server.send(302, "text/plain", "");
  });

  // ── Face management API ─────────────────────────────────────────────────────
  server.on("/api/buzzer/test", HTTP_POST, [&server, &sm]() {
    if (!requireAuth(server)) return;
    if (!SessionAuth::verifyCsrf(server.arg("csrf"))) {
      server.send(403, "text/plain", "CSRF validation failed.");
      return;
    }
    if (sm.isAlarmActive()) {
      // Alarm buzzer (or auto-silenced alarm) takes priority; refuse test during active alarm
      server.send(409, "text/plain", "Alarm active");
      return;
    }
    uint32_t freq = ConfigManager::getBuzzerFreq();
    String freqArg = server.arg("freq");
    if (freqArg.length() > 0) {
      int f = freqArg.toInt();
      if (f >= 200 && f <= 8000) freq = (uint32_t)f;
    }
    BuzzerController::testBeep(freq, BUZZER_TEST_DURATION_MS);
    server.send(200, "text/plain", "OK");
  });

  server.on("/api/face/enroll", HTTP_POST, [&server]() {
    if (!requireAuthAndChangedPassword(server)) return;
    if (!SessionAuth::verifyCsrf(server.arg("csrf"))) {
      server.send(403, "application/json", "{\"error\":\"CSRF\"}");
      return;
    }
    if (!CameraAgent::isInitialized()) {
      server.send(503, "application/json", "{\"error\":\"camera not ready\"}");
      return;
    }
    String name = server.arg("name");
    name.trim();
    String sanitized;
    for (size_t i = 0; i < name.length() && (int)sanitized.length() < FaceRecognizer::MAX_NAME_LEN; i++) {
      char c = name[i];
      if (c >= 32 && c < 127) sanitized += c;
    }
    name = sanitized;
    // canEnroll() checks: existing user → template slots; new user → user slots
    if (!FaceRecognizer::canEnroll(name.length() > 0 ? name.c_str() : nullptr)) {
      server.send(409, "application/json", "{\"error\":\"face bank full\"}");
      return;
    }
    CameraAgent::scheduleEnroll(name.length() > 0 ? name.c_str() : nullptr);
    JsonDocument doc;
    doc["scheduled"] = true;
    doc["count"]     = FaceRecognizer::count();
    doc["max"]       = FaceRecognizer::MAX_FACES;
    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
  });

  server.on("/api/face/list", HTTP_GET, [&server]() {
    if (!requireAuthAndChangedPassword(server)) return;
    JsonDocument doc;
    JsonArray faces = doc["faces"].to<JsonArray>();
    for (int i = 0; i < FaceRecognizer::count(); i++) {
      const char* n = FaceRecognizer::getName(i);
      faces.add(n ? n : "");
    }
    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
  });

  server.on("/api/face/clear", HTTP_POST, [&server]() {
    if (!requireAuthAndChangedPassword(server)) return;
    if (!SessionAuth::verifyCsrf(server.arg("csrf"))) {
      server.send(403, "application/json", "{\"error\":\"CSRF\"}");
      return;
    }
    CameraAgent::cancelEnroll();
    bool ok = FaceRecognizer::clearAll();
    JsonDocument doc;
    doc["cleared"] = ok;
    if (!ok) doc["error"] = "NVS write failed";
    String json;
    serializeJson(doc, json);
    server.send(ok ? 200 : 500, "application/json", json);
  });

  // ── 404 fallback ────────────────────────────────────────────────────────────
  server.onNotFound([&server]() {
    server.sendHeader("Location", "/dashboard");
    server.send(302, "text/plain", "");
  });
}
