import * as THREE from 'three';
import {GLTFLoader} from 'https://cdn.jsdelivr.net/npm/three@0.180.0/examples/jsm/loaders/GLTFLoader.js';

const MODEL='https://cdn.jsdelivr.net/gh/gwihlidal/svc-mesh@d7281abc9be106408621da05797ad7efb0d6aaeb/data/EpicCitadel.glb';
const canvas=document.querySelector('#view'),status=document.querySelector('#status');
const progress=document.querySelector('#progress'),bar=progress.querySelector('i'),enter=document.querySelector('#enter'),touch=document.querySelector('#touch');

function fail(message,error){ console.error(message,error||''); status.textContent=message; progress.classList.add('error'); }
addEventListener('error',e=>fail('Renderer error: '+e.message,e.error));
addEventListener('unhandledrejection',e=>fail('Load error: '+(e.reason?.message||e.reason),e.reason));

let renderer;
try {
  renderer=new THREE.WebGLRenderer({canvas,antialias:true,powerPreference:'high-performance'});
} catch(e) { fail('WebGL could not start on this browser',e); throw e; }
renderer.setPixelRatio(Math.min(devicePixelRatio,2));
renderer.outputColorSpace=THREE.SRGBColorSpace;
renderer.toneMapping=THREE.ACESFilmicToneMapping;

const scene=new THREE.Scene(); scene.background=new THREE.Color(0x9eb8c7);
const camera=new THREE.PerspectiveCamera(65,1,.1,100000);
scene.add(new THREE.HemisphereLight(0xddeeff,0x4d4438,2));
const sun=new THREE.DirectionalLight(0xfff2d0,2); sun.position.set(2,5,3); scene.add(sun);

let yaw=0,pitch=-.08,speed=250,drag=false,lastX=0,lastY=0,ready=false;
const keys=new Set(), loader=new GLTFLoader(); loader.setCrossOrigin('anonymous');
status.textContent='Downloading Citadel scene…';
async function loadModel(){
  try {
    status.textContent='Downloading Citadel scene…';
    const res=await fetch(MODEL,{mode:'cors',cache:'force-cache'});
    if(!res.ok) throw new Error('HTTP '+res.status+' '+res.statusText);
    const total=Number(res.headers.get('content-length'))||0;
    let data;
    if(res.body&&total){
      const reader=res.body.getReader(),chunks=[];let loaded=0;
      while(true){const {done,value}=await reader.read();if(done)break;chunks.push(value);loaded+=value.byteLength;const p=Math.min(100,loaded/total*100);bar.style.width=p+'%';status.textContent='Downloading Citadel scene… '+p.toFixed(0)+'%';}
      data=new Uint8Array(loaded);let o=0;for(const c of chunks){data.set(c,o);o+=c.byteLength;}
      data=data.buffer;
    } else data=await res.arrayBuffer();
    status.textContent='Parsing Citadel scene…';
    loader.parse(data,'',onModelLoaded,e=>fail('Citadel parse failed: '+(e?.message||e),e));
  } catch(e){ fail('Citadel download failed: '+(e?.message||e),e); }
}
function onModelLoaded(gltf){
  scene.add(gltf.scene);
  const box=new THREE.Box3().setFromObject(gltf.scene),size=box.getSize(new THREE.Vector3()),center=box.getCenter(new THREE.Vector3()),span=Math.max(size.x,size.y,size.z);
  camera.near=Math.max(.05,span/100000); camera.far=span*20; camera.updateProjectionMatrix();
  camera.position.set(center.x,center.y+size.y*.12,center.z+span*.35); camera.lookAt(center);
  const e=new THREE.Euler().setFromQuaternion(camera.quaternion,'YXZ'); pitch=e.x;yaw=e.y;speed=Math.max(1,span*.08);
  status.textContent='Citadel loaded — drag to look and move';enter.hidden=false;ready=true;progress.classList.add('done');
  if(matchMedia('(pointer: coarse)').matches) touch.hidden=false;
}
loadModel();

function resize(){const w=innerWidth,h=innerHeight;renderer.setSize(w,h,false);camera.aspect=w/h;camera.updateProjectionMatrix()} addEventListener('resize',resize);resize();
function begin(){if(!ready)return;enter.hidden=true;if(!matchMedia('(pointer: coarse)').matches)canvas.requestPointerLock?.()}
enter.addEventListener('click',begin);canvas.addEventListener('click',begin);
addEventListener('keydown',e=>keys.add(e.code));addEventListener('keyup',e=>keys.delete(e.code));
canvas.addEventListener('pointerdown',e=>{drag=true;lastX=e.clientX;lastY=e.clientY;canvas.setPointerCapture?.(e.pointerId)});
canvas.addEventListener('pointerup',()=>drag=false);
canvas.addEventListener('pointermove',e=>{const locked=document.pointerLockElement===canvas;if(!locked&&!drag)return;const dx=locked?e.movementX:e.clientX-lastX,dy=locked?e.movementY:e.clientY-lastY;lastX=e.clientX;lastY=e.clientY;yaw-=dx*.0022;pitch-=dy*.0022;pitch=Math.max(-1.5,Math.min(1.5,pitch));});
addEventListener('wheel',e=>{speed*=e.deltaY>0?.85:1.18;speed=Math.max(.1,Math.min(speed,10000))},{passive:true});
for(const b of document.querySelectorAll('[data-key]')){const k=b.dataset.key;b.addEventListener('pointerdown',e=>{e.preventDefault();keys.add(k)});for(const ev of ['pointerup','pointercancel','pointerleave'])b.addEventListener(ev,()=>keys.delete(k));}

const clock=new THREE.Clock(),forward=new THREE.Vector3(),right=new THREE.Vector3();
function frame(){requestAnimationFrame(frame);const dt=Math.min(clock.getDelta(),.05);camera.rotation.set(pitch,yaw,0,'YXZ');forward.set(-Math.sin(yaw),0,-Math.cos(yaw));right.set(Math.cos(yaw),0,-Math.sin(yaw));const mult=(keys.has('ShiftLeft')||keys.has('ShiftRight'))?3:1,step=speed*mult*dt;if(keys.has('KeyW'))camera.position.addScaledVector(forward,step);if(keys.has('KeyS'))camera.position.addScaledVector(forward,-step);if(keys.has('KeyA'))camera.position.addScaledVector(right,-step);if(keys.has('KeyD'))camera.position.addScaledVector(right,step);if(keys.has('Space'))camera.position.y+=step;if(keys.has('ControlLeft')||keys.has('KeyC'))camera.position.y-=step;renderer.render(scene,camera)} frame();
