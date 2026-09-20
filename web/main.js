import * as THREE from 'https://cdn.jsdelivr.net/npm/three@0.180.0/build/three.module.js';
import {GLTFLoader} from 'https://cdn.jsdelivr.net/npm/three@0.180.0/examples/jsm/loaders/GLTFLoader.js';

const ASSET_ROOT='https://raw.githubusercontent.com/Phyronnaz/DAG_Compression/master/assets/EpicCitadel/glTF/';
const MODEL=ASSET_ROOT+'EpicCitadel.gltf';
const canvas=document.querySelector('#view'), status=document.querySelector('#status');
const progress=document.querySelector('#progress'), bar=progress.querySelector('i'), enter=document.querySelector('#enter');

const renderer=new THREE.WebGLRenderer({canvas,antialias:true,powerPreference:'high-performance'});
renderer.setPixelRatio(Math.min(devicePixelRatio,2));
renderer.outputColorSpace=THREE.SRGBColorSpace;
renderer.toneMapping=THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure=1.0;

const scene=new THREE.Scene();
scene.background=new THREE.Color(0x9eb8c7);
const camera=new THREE.PerspectiveCamera(65,1,.1,100000);
scene.add(new THREE.HemisphereLight(0xddeeff,0x4d4438,2.0));
const sun=new THREE.DirectionalLight(0xfff2d0,2.0); sun.position.set(2,5,3); scene.add(sun);

let yaw=0,pitch=-.08,speed=250,drag=false,lastX=0,lastY=0,ready=false;
const keys=new Set();
const loader=new GLTFLoader();
loader.setCrossOrigin('anonymous');
loader.load(MODEL,gltf=>{
  scene.add(gltf.scene);
  const box=new THREE.Box3().setFromObject(gltf.scene), size=box.getSize(new THREE.Vector3()), center=box.getCenter(new THREE.Vector3());
  const span=Math.max(size.x,size.y,size.z);
  camera.near=Math.max(.05,span/100000); camera.far=span*20; camera.updateProjectionMatrix();
  camera.position.set(center.x, center.y+size.y*.12, center.z+span*.35);
  camera.lookAt(center);
  const e=new THREE.Euler().setFromQuaternion(camera.quaternion,'YXZ'); pitch=e.x; yaw=e.y;
  speed=Math.max(1,span*.08);
  status.textContent='Citadel loaded — click/tap the scene';
  enter.hidden=false; ready=true; progress.classList.add('done');
},xhr=>{
  if(xhr.total){const p=Math.min(100,xhr.loaded/xhr.total*100);bar.style.width=p+'%';status.textContent='Loading Citadel… '+p.toFixed(0)+'%';}
},err=>{console.error(err);status.textContent='Scene load failed — see browser console';});

function resize(){const w=innerWidth,h=innerHeight;renderer.setSize(w,h,false);camera.aspect=w/h;camera.updateProjectionMatrix()} addEventListener('resize',resize);resize();
function begin(){canvas.requestPointerLock?.();enter.hidden=true}
enter.addEventListener('click',begin);canvas.addEventListener('click',()=>{if(ready)begin()});
addEventListener('keydown',e=>keys.add(e.code));addEventListener('keyup',e=>keys.delete(e.code));
addEventListener('pointerdown',e=>{drag=true;lastX=e.clientX;lastY=e.clientY});
addEventListener('pointerup',()=>drag=false);
addEventListener('pointermove',e=>{
  const locked=document.pointerLockElement===canvas;
  if(!locked&&!drag)return;
  const dx=locked?e.movementX:e.clientX-lastX,dy=locked?e.movementY:e.clientY-lastY;lastX=e.clientX;lastY=e.clientY;
  yaw-=dx*.0022;pitch-=dy*.0022;pitch=Math.max(-1.5,Math.min(1.5,pitch));
});
addEventListener('wheel',e=>{speed*=e.deltaY>0?.85:1.18;speed=Math.max(.1,Math.min(speed,10000))},{passive:true});

const clock=new THREE.Clock(), forward=new THREE.Vector3(), right=new THREE.Vector3();
function frame(){
  requestAnimationFrame(frame);const dt=Math.min(clock.getDelta(),.05);
  camera.rotation.set(pitch,yaw,0,'YXZ');
  forward.set(-Math.sin(yaw),0,-Math.cos(yaw));right.set(Math.cos(yaw),0,-Math.sin(yaw));
  const mult=(keys.has('ShiftLeft')||keys.has('ShiftRight'))?3:1, step=speed*mult*dt;
  if(keys.has('KeyW'))camera.position.addScaledVector(forward,step);
  if(keys.has('KeyS'))camera.position.addScaledVector(forward,-step);
  if(keys.has('KeyA'))camera.position.addScaledVector(right,-step);
  if(keys.has('KeyD'))camera.position.addScaledVector(right,step);
  if(keys.has('Space'))camera.position.y+=step;
  if(keys.has('ControlLeft')||keys.has('KeyC'))camera.position.y-=step;
  renderer.render(scene,camera);
} frame();
