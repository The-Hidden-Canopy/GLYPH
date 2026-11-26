import assert from "node:assert/strict";
import {readFile} from "node:fs/promises";
import test from "node:test";
import vm from "node:vm";
test("shell theme uses only bounded preference and system changes", async () => {
 const source=await readFile(new URL("chrome.js",import.meta.url),"utf8");
 let stored="invalid",change,systemChange;
 const select={value:"",addEventListener:(_,fn)=>{change=fn;}};
 const dataset={};
 const media={matches:true,addEventListener:(_,fn)=>{systemChange=fn;}};
 vm.runInNewContext(source,{
   document:{getElementById:()=>select,documentElement:{dataset}},
   localStorage:{getItem:()=>stored,setItem:(_,v)=>{stored=v;}},
   window:{matchMedia:()=>media},
 });
 assert.equal(dataset.theme,"dark");
 media.matches=false;systemChange();assert.equal(dataset.theme,"light");
 select.value="dark";change();assert.equal(stored,"dark");
 media.matches=false;systemChange();assert.equal(dataset.theme,"dark");
 select.value="invalid";change();assert.equal(stored,"system");assert.equal(dataset.theme,"light");
});
