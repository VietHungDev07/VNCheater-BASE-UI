/* GPU effects use the supplied logo as a texture; original image files are unchanged. */
(() => {
  const reduced = matchMedia('(prefers-reduced-motion: reduce)');
  let quality = reduced.matches ? 'still' : 'cinematic';
  let hidden = false, scene = 'intro', epoch = performance.now(), pointer = [.5,.5];
  const vertex = 'attribute vec2 a; varying vec2 uv; void main(){uv=a*.5+.5;gl_Position=vec4(a,0.,1.);}';
  const logoFragment = `precision mediump float;
    varying vec2 uv; uniform sampler2D logo; uniform float time; uniform float still;
    float hash(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
    void main(){
      vec2 p=uv-.5; float r=length(p); float t=time;
      float reveal=still>.5?1.:smoothstep(.15,2.6,t);
      vec2 q=p/vec2(.64,.72)+.5;
      float edge=smoothstep(0.,.07,q.x)*(1.-smoothstep(.93,1.,q.x))*smoothstep(0.,.06,q.y)*(1.-smoothstep(.94,1.,q.y));
      float rip=sin(r*48.-t*1.8)*.0018*(1.-smoothstep(1.,3.,t));
      vec2 tex=vec2(mix(.16,.83,q.x+rip),mix(.145,.995,q.y));
      vec4 samp=texture2D(logo,clamp(tex,0.,1.));
      vec3 c=samp.rgb;
      float lum=max(c.r,max(c.g,c.b))*samp.a;
      float cut=smoothstep(.1,.46,lum)*edge;
      float front=reveal*1.28-.14;
      float mask=1.-smoothstep(front-.06,front+.06,q.y);
      float shine=exp(-pow((q.y-front)*35.,2.))*.7;
      vec3 color=c*cut*mask*(1.+shine);
      color+=vec3(.42,.9,1.)*shine*cut*.5;
      float ang=atan(p.y,p.x);
      float ring=exp(-abs(r-.405)*450.);
      float arc=pow(max(0.,cos(ang-t*.33)),18.);
      color+=ring*(.04+arc*.3)*vec3(.62,.88,1.)*reveal;
      float halo=exp(-pow((r-.3)*8.,2.))*.018;
      color+=halo*vec3(.45,.82,1.)*reveal;
      float water=exp(-abs(p.y+.395)*95.)*exp(-p.x*p.x*50.)*.085;
      color+=water*(.7+.3*sin(p.x*90.+t*1.7))*vec3(.7,.92,1.);
      float alpha=max(max(color.r,color.g),color.b);
      gl_FragColor=vec4(color,alpha);
    }`;
  const atmosphereFragment = `precision mediump float;
    varying vec2 uv; uniform vec2 resolution; uniform float time; uniform float rainy; uniform vec2 pointer;
    float hash(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
    void main(){
      vec2 p=uv;float t=time;float light=0.;
      for(int i=0;i<12;i++){
        float f=float(i);vec2 seed=vec2(f,f+2.);
        vec2 pos=vec2(fract(hash(seed)+t*(.002+f*.00009)),fract(hash(seed+2.)+t*.009));
        pos.x+=sin(t*.3+f)*.015;vec2 d=(p-pos)*vec2(resolution.x/resolution.y,1.);
        float radius=.0015+hash(seed+4.)*.003;
        light+=exp(-dot(d,d)/(radius*radius))*(.15+.12*sin(f+t*.6));
      }
      vec2 rain=vec2(p.x*80.+p.y*12.,p.y*8.+t*1.15);
      vec2 cell=floor(rain);vec2 local=fract(rain);
      float drop=exp(-pow((local.x-.5)*70.,2.))*smoothstep(.1,.75,local.y)*(1.-smoothstep(.75,1.,local.y));
      drop*=step(.83,hash(vec2(cell.x,0.)))*.11*rainy;
      float rays=pow(max(0.,sin(p.x*8.+p.y*2.+sin(t*.08)*.5)),16.)*.009;
      float cursor=exp(-dot(p-pointer,p-pointer)*32.)*.013;
      gl_FragColor=vec4(vec3(.78,.94,1.)*(light+drop+rays+cursor),1.);
    }`;
  function surface(id, fragment, isLogo) {
    const canvas = document.getElementById(id);
    const gl = canvas.getContext('webgl',{alpha:!isLogo?false:true,premultipliedAlpha:true,antialias:false,powerPreference:'low-power',preserveDrawingBuffer:false});
    if (!gl) return null;
    let program, buffer, texture, uniforms={}, frame=0, last=0, ready=!isLogo, failed=false;
    function compile(type, source){const s=gl.createShader(type);gl.shaderSource(s,source);gl.compileShader(s);if(!gl.getShaderParameter(s,gl.COMPILE_STATUS)){const err=gl.getShaderInfoLog(s);gl.deleteShader(s);throw new Error(err);}return s;}
    function init(){
      try{
        const v=compile(gl.VERTEX_SHADER,vertex),f=compile(gl.FRAGMENT_SHADER,fragment);
        program=gl.createProgram();gl.attachShader(program,v);gl.attachShader(program,f);gl.linkProgram(program);gl.deleteShader(v);gl.deleteShader(f);
        if(!gl.getProgramParameter(program,gl.LINK_STATUS))throw new Error(gl.getProgramInfoLog(program));
        gl.useProgram(program);buffer=gl.createBuffer();gl.bindBuffer(gl.ARRAY_BUFFER,buffer);
        gl.bufferData(gl.ARRAY_BUFFER,new Float32Array([-1,-1,1,-1,-1,1,-1,1,1,-1,1,1]),gl.STATIC_DRAW);
        const a=gl.getAttribLocation(program,'a');gl.enableVertexAttribArray(a);gl.vertexAttribPointer(a,2,gl.FLOAT,false,0,0);
        ['time','still','logo','resolution','rainy','pointer'].forEach(n=>uniforms[n]=gl.getUniformLocation(program,n));
        if(isLogo){texture=gl.createTexture();const img=new Image();img.onload=()=>{if(failed)return;gl.bindTexture(gl.TEXTURE_2D,texture);gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL,true);gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA,gl.RGBA,gl.UNSIGNED_BYTE,img);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER,gl.LINEAR);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MAG_FILTER,gl.LINEAR);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_S,gl.CLAMP_TO_EDGE);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_T,gl.CLAMP_TO_EDGE);ready=true;document.getElementById('introEmblem').classList.add('shader-ready');resume();};img.onerror=()=>{failed=true;canvas.hidden=true;};img.src='assets/images/logo.jpg';}
        resize();resume();
      }catch(e){failed=true;canvas.hidden=true;document.getElementById('introEmblem').classList.remove('shader-ready');console.warn('Using static effect fallback:',e.message);}
    }
    function resize(){const rect=canvas.getBoundingClientRect();const scale=isLogo?Math.min(devicePixelRatio,1.5):quality==='cinematic'?.7:.5;canvas.width=Math.max(1,Math.round(rect.width*scale));canvas.height=Math.max(1,Math.round(rect.height*scale));gl.viewport(0,0,canvas.width,canvas.height);}
    function draw(now){frame=0;if(failed||hidden||document.hidden||!ready||isLogo&&scene!=='intro')return;
      const stationary=quality==='still'||reduced.matches;
      if(!stationary&&now-last<1000/(quality==='balanced'?20:30)){frame=requestAnimationFrame(draw);return;}last=now;
      gl.useProgram(program);gl.uniform1f(uniforms.time,stationary?4:(now-epoch)/1000);gl.uniform1f(uniforms.still,stationary?1:0);
      if(!isLogo){gl.uniform2f(uniforms.resolution,canvas.width,canvas.height);gl.uniform1f(uniforms.rainy,scene==='intro'||scene==='auth'?1:0);gl.uniform2f(uniforms.pointer,pointer[0],pointer[1]);}
      if(isLogo){gl.clearColor(0,0,0,0);gl.clear(gl.COLOR_BUFFER_BIT);}
      gl.drawArrays(gl.TRIANGLES,0,6);if(!stationary)frame=requestAnimationFrame(draw);
    }
    function resume(){cancelAnimationFrame(frame);frame=requestAnimationFrame(draw);}
    new ResizeObserver(()=>{resize();resume();}).observe(canvas);
    canvas.addEventListener('webglcontextlost',e=>{e.preventDefault();failed=true;cancelAnimationFrame(frame);canvas.hidden=true;if(isLogo)document.getElementById('introEmblem').classList.remove('shader-ready');});
    canvas.addEventListener('webglcontextrestored',()=>{failed=false;canvas.hidden=false;init();});
    init();return {resume};
  }
  const logo=surface('logoShader',logoFragment,true), ambient=surface('atmosphere',atmosphereFragment,false);
  document.getElementById('atmosphere').style.mixBlendMode='screen';
  function resume(){logo?.resume();ambient?.resume();}
  document.addEventListener('visibilitychange',resume);
  window.addEventListener('pointermove',e=>{pointer=[e.clientX/innerWidth,1-e.clientY/innerHeight];if(quality!=='still'&&!reduced.matches){document.documentElement.style.setProperty('--mx',`${(pointer[0]-.5)*-5}px`);document.documentElement.style.setProperty('--my',`${(pointer[1]-.5)*3}px`);}},{passive:true});
  window.LauncherEffects={scene(name){scene=name;if(name==='intro')epoch=performance.now();resume();},quality(value){quality=value;document.body.dataset.motion=value;resume();},pause(value){hidden=value;document.body.classList.toggle('paused',value);resume();}};
  reduced.addEventListener('change',()=>{if(reduced.matches)window.LauncherEffects.quality('still');resume();});
})();
