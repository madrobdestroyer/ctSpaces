// Verify the delivered local player and decode the actual MP4 narration.
const fs=require('fs');
const path=require('path');
const {pathToFileURL}=require('url');
const {chromium}=require('playwright');
const assert=require('assert');
const root=__dirname,output=path.join(root,'output');
(async()=>{
  const browser=await chromium.launch({headless:true,
    executablePath:'C:/Program Files/Google/Chrome/Application/chrome.exe',
    args:['--disable-background-networking','--mute-audio']});
  try {
    const page=await browser.newPage({viewport:{width:1440,height:1080}});
    const errors=[];page.on('pageerror',e=>errors.push(e.message));
    await page.route('http**',r=>r.abort());
    await page.goto(pathToFileURL(path.join(output,'index.html')).href);
    await page.waitForFunction(()=>document.getElementById('video').readyState>=1);
    const result=[];
    for(const [index,key] of ['ctSpaces','Notes'].entries()) {
      await page.getByRole('button',{name:index?'Client Notes':'ctSpaces',exact:true}).click();
      await page.waitForFunction(()=>document.getElementById('video').readyState>=1);
      const plan=JSON.parse(fs.readFileSync(path.join(root,'timing.json'),'utf8')).videos[index];
      const chapters=page.locator('#chapters button');
      assert.equal(await chapters.count(),plan.chapters.length);
      await chapters.nth(plan.chapters.length-1).click();
      await page.waitForFunction(start=>Math.abs(document.getElementById('video').currentTime-start)<2,plan.chapters.at(-1).start);
      await page.waitForFunction(()=>{const v=document.getElementById('video');return !v.seeking&&v.readyState>=2});
      await page.waitForTimeout(200);
      await page.evaluate(()=>document.getElementById('video').pause());
      assert.equal(await chapters.last().getAttribute('aria-current'),'true');
      await page.getByRole('button',{name:'Captions on',exact:true}).click();
      assert.equal(await page.locator('#captions').getAttribute('aria-pressed'),'false');
      await page.getByRole('button',{name:'Captions off',exact:true}).click();
      await page.waitForFunction(()=>document.querySelector('#caption span')?.textContent.length>0);
      await page.screenshot({path:path.join(output,key+'-player.png'),fullPage:true});
      await page.evaluate(()=>{const input=document.createElement('input');input.type='file';input.id='decode-only';input.style.display='none';document.body.append(input)});
      await page.locator('#decode-only').setInputFiles(path.join(output,key+'-Walkthrough.mp4'));
      const audio=await page.evaluate(async()=>{
        const file=document.getElementById('decode-only').files[0];
        const context=new OfflineAudioContext(1,1,48000);
        const decoded=await context.decodeAudioData(await file.arrayBuffer());
        const samples=decoded.getChannelData(0);let sum=0,peak=0,nonzero=0;
        for(const sample of samples){sum+=sample*sample;peak=Math.max(peak,Math.abs(sample));if(Math.abs(sample)>.001)nonzero++}
        return {duration:decoded.duration,sampleRate:decoded.sampleRate,channels:decoded.numberOfChannels,
          rms:Math.sqrt(sum/samples.length),peak,nonzeroFraction:nonzero/samples.length};
      });
      console.log(JSON.stringify({video:key,expectedDuration:plan.duration,decodedAudio:audio}));
      const inspection=JSON.parse(fs.readFileSync(path.join(output,key+'-inspection.json'),'utf8'));
      // Windows precise trimming can retain a fractional final video sample.
      // Require the decoded audio to match that measured container duration.
      assert(Math.abs(audio.duration-inspection.durationSeconds)<.08);
      assert(Math.abs(audio.duration-plan.duration)<.5);
      assert(audio.rms>.01 && audio.nonzeroFraction>.25,'Decoded MP4 narration is silent or missing');
      result.push({video:key,chapters:plan.chapters.length,seeking:true,captions:true,decodedAudio:audio});
      await page.locator('#decode-only').evaluate(e=>e.remove());
    }
    await page.setViewportSize({width:420,height:900});
    assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
    await page.screenshot({path:path.join(output,'player-mobile.png'),fullPage:true});
    assert.deepEqual(errors,[]);
    fs.writeFileSync(path.join(output,'player-check.json'),JSON.stringify({passed:true,errors,mobileNoHorizontalOverflow:true,result},null,2));
    console.log(JSON.stringify(result,null,2));
  } finally {await browser.close()}
})().catch(e=>{console.error(e);process.exitCode=1});
