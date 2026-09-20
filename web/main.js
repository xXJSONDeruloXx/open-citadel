import * as THREE from 'three';
import {GLTFLoader} from 'https://cdn.jsdelivr.net/npm/three@0.180.0/examples/jsm/loaders/GLTFLoader.js';

const MODEL='https://cdn.jsdelivr.net/gh/gwihlidal/svc-mesh@d7281abc9be106408621da05797ad7efb0d6aaeb/data/EpicCitadel.glb';
const TEX_ROOT='https://cdn.jsdelivr.net/gh/Phyronnaz/DAG_Compression@d0f9ea3d9aea4cdc13b5b89e63eb03722965add0/assets/EpicCitadel/glTF/';
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

let yaw=0,pitch=-.08,speed=600,drag=false,lastX=0,lastY=0,ready=false,world=null;
const EYE_HEIGHT=165;
const keys=new Set(), loader=new GLTFLoader(); loader.setCrossOrigin('anonymous');
status.textContent='Downloading Citadel scene…';
async function loadModel(){
  try {
    status.textContent='Downloading Citadel scene…';
    const res=await fetch(MODEL,{mode:'cors',cache:'force-cache'});
    if(!res.ok) throw new Error('HTTP '+res.status+' '+res.statusText);
    const data=await res.arrayBuffer();
    status.textContent='Parsing Citadel scene…';
    loader.parse(data,'',onModelLoaded,e=>fail('Citadel parse failed: '+(e?.message||e),e));
  } catch(e){ fail('Citadel download failed: '+(e?.message||e),e); }
}
async function applyCitadelMaterials(root){
  status.textContent='Loading Citadel textures…';
  const defs=await fetch('./materials.json',{cache:'force-cache'}).then(r=>{if(!r.ok)throw new Error('material map HTTP '+r.status);return r.json()});
  const loader=new THREE.TextureLoader(); loader.setCrossOrigin('anonymous');
  const cache=new Map(); const mats=new Map();
  root.traverse(o=>{if(o.isMesh){for(const m of (Array.isArray(o.material)?o.material:[o.material])) if(m)mats.set(m.name,m)}});
  let done=0; const jobs=[];
  for(const m of mats.values()){
    const key=m.name.replace('_M_','_T_'),d=defs[key]; if(!d)continue;
    let tex=cache.get(d.uri);
    if(!tex){tex=loader.load(TEX_ROOT+encodeURIComponent(d.uri),()=>{},undefined,e=>console.warn('texture failed',d.uri,e));tex.colorSpace=THREE.SRGBColorSpace;tex.wrapS=tex.wrapT=THREE.RepeatWrapping;cache.set(d.uri,tex)}
    m.map=tex;m.color.setRGB(1,1,1);m.side=d.doubleSided?THREE.DoubleSide:THREE.FrontSide;m.needsUpdate=true;done++;
  }
  status.textContent='Applied '+done+' Citadel materials…';
}
function chooseGroundStart(root,box,size,center){
  const ray=new THREE.Raycaster(),down=new THREE.Vector3(0,-1,0),hits=[];
  const top=box.max.y+1000;
  for(let ix=-4;ix<=4;ix++)for(let iz=-4;iz<=4;iz++){
    const x=center.x+size.x*ix/10,z=center.z+size.z*iz/10;
    ray.set(new THREE.Vector3(x,top,z),down);
    const h=ray.intersectObject(root,true).find(q=>q.point.y < box.min.y+size.y*.48);
    if(h)hits.push(h.point.clone());
  }
  hits.sort((a,b)=>a.distanceToSquared(center)-b.distanceToSquared(center));
  const p=hits[0]||new THREE.Vector3(center.x,box.min.y,center.z);
  camera.position.set(p.x,p.y+EYE_HEIGHT,p.z); yaw=Math.PI;pitch=-.03;speed=600;
}
async function onModelLoaded(gltf){
  world=gltf.scene; scene.add(world);
  const box=new THREE.Box3().setFromObject(world),size=box.getSize(new THREE.Vector3()),center=box.getCenter(new THREE.Vector3()),span=Math.max(size.x,size.y,size.z);
  camera.near=5;camera.far=span*20;camera.updateProjectionMatrix();
  chooseGroundStart(world,box,size,center);
  try{await applyCitadelMaterials(world)}catch(e){console.warn(e);status.textContent='Citadel geometry loaded; some textures unavailable';}
  status.textContent='Citadel loaded — walk with the controls and drag to look';enter.hidden=false;ready=true;progress.classList.add('done');
  if(matchMedia('(pointer: coarse)').matches)touch.hidden=false;
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
function frame(){requestAnimationFrame(frame);const dt=Math.min(clock.getDelta(),.05);camera.rotation.set(pitch,yaw,0,'YXZ');forward.set(-Math.sin(yaw),0,-Math.cos(yaw));right.set(Math.cos(yaw),0,-Math.sin(yaw));const mult=(keys.has('ShiftLeft')||keys.has('ShiftRight'))?3:1,step=speed*mult*dt;if(keys.has('KeyW'))camera.position.addScaledVector(forward,step);if(keys.has('KeyS'))camera.position.addScaledVector(forward,-step);if(keys.has('KeyA'))camera.position.addScaledVector(right,-step);if(keys.has('KeyD'))camera.position.addScaledVector(right,step);if(keys.has('Space'))camera.position.y+=step;if(keys.has('ControlLeft')||keys.has('KeyC'))camera.position.y-=step;if(world&&!keys.has('Space')&&!keys.has('ControlLeft')&&!keys.has('KeyC')){const r=new THREE.Raycaster(new THREE.Vector3(camera.position.x,camera.position.y+400, camera.position.z),new THREE.Vector3(0,-1,0),0,900);const h=r.intersectObject(world,true)[0];if(h&&Math.abs((h.point.y+EYE_HEIGHT)-camera.position.y)<350)camera.position.y=h.point.y+EYE_HEIGHT;}renderer.render(scene,camera)} frame();
