/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/* global BigInt */

"use strict";

function matchingProperties(obj, regexp) {
  return Object.getOwnPropertyNames(obj)
    .filter(n => regexp.test(n))
    .map(n => obj[n])
    .filter(v => typeof v == "function");
}

function allProperties(obj) {
  return matchingProperties(obj, /./);
}

module.exports = [
  Array,
  Array.from,
  Array.isArray,
  Array.of,
  Array.prototype.concat,
  Array.prototype.entries,
  Array.prototype.every,
  Array.prototype.fill,
  Array.prototype.filter,
  Array.prototype.find,
  Array.prototype.findIndex,
  Array.prototype.flat,
  Array.prototype.flatMap,
  Array.prototype.forEach,
  Array.prototype.includes,
  Array.prototype.indexOf,
  Array.prototype.join,
  Array.prototype.keys,
  Array.prototype.lastIndexOf,
  Array.prototype.map,
  Array.prototype.reduce,
  Array.prototype.reduceRight,
  Array.prototype.slice,
  Array.prototype.some,
  Array.prototype.values,
  ArrayBuffer,
  ArrayBuffer.isView,
  ArrayBuffer.prototype.slice,
  BigInt,
  ...allProperties(BigInt),
  Boolean,
  DataView,
  Date,
  Date.now,
  Date.parse,
  Date.UTC,
  ...matchingProperties(Date.prototype, /^get/),
  ...matchingProperties(Date.prototype, /^to.*?String$/),
  Error,
  Function,
  Function.prototype.apply,
  Function.prototype.bind,
  Function.prototype.call,
  Int8Array,
  Uint8Array,
  Uint8ClampedArray,
  Int16Array,
  Uint16Array,
  Int32Array,
  Uint32Array,
  Float32Array,
  Float64Array,
  // These will apply to other typed array prototypes.
  Int8Array.prototype.entries,
  Int8Array.prototype.every,
  Int8Array.prototype.filter,
  Int8Array.prototype.find,
  Int8Array.prototype.findIndex,
  Int8Array.prototype.forEach,
  Int8Array.prototype.indexOf,
  Int8Array.prototype.includes,
  Int8Array.prototype.join,
  Int8Array.prototype.keys,
  Int8Array.prototype.lastIndexOf,
  Int8Array.prototype.map,
  Int8Array.prototype.reduce,
  Int8Array.prototype.reduceRight,
  Int8Array.prototype.slice,
  Int8Array.prototype.some,
  Int8Array.prototype.subarray,
  Int8Array.prototype.values,
  ...allProperties(JSON),
  Map,
  Map.prototype.forEach,
  Map.prototype.get,
  Map.prototype.has,
  Map.prototype.entries,
  Map.prototype.keys,
  Map.prototype.values,
  ...allProperties(Math),
  Number,
  ...allProperties(Number),
  ...allProperties(Number.prototype),
  Object,
  Object.create,
  Object.keys,
  Object.entries,
  Object.getOwnPropertyDescriptor,
  Object.getOwnPropertyDescriptors,
  Object.getOwnPropertyNames,
  Object.getOwnPropertySymbols,
  Object.getPrototypeOf,
  Object.is,
  Object.isExtensible,
  Object.isFrozen,
  Object.isSealed,
  Object.values,
  Object.prototype.hasOwnProperty,
  Object.prototype.isPrototypeOf,
  Proxy,
  Proxy.revocable,
  Reflect.apply,
  Reflect.construct,
  Reflect.get,
  Reflect.getOwnPropertyDescriptor,
  Reflect.getPrototypeOf,
  Reflect.has,
  Reflect.isExtensible,
  Reflect.ownKeys,
  RegExp,
  RegExp.prototype.exec,
  RegExp.prototype.test,
  Set,
  Set.prototype.entries,
  Set.prototype.forEach,
  Set.prototype.has,
  Set.prototype.values,
  String,
  ...allProperties(String),
  ...allProperties(String.prototype),
  Symbol,
  Symbol.keyFor,
  WeakMap,
  WeakMap.prototype.get,
  WeakMap.prototype.has,
  WeakSet,
  WeakSet.prototype.has,
  decodeURI,
  decodeURIComponent,
  encodeURI,
  encodeURIComponent,
  escape,
  isFinite,
  isNaN,
  unescape,
];