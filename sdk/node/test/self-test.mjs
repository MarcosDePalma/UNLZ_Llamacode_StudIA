import assert from "node:assert/strict";
import { FrameDecoder, PROTOCOL, encodeFrame } from "../index.mjs";

const frame = encodeFrame({ protocol: PROTOCOL, type: "hello", nonce: "n" });
const decoder = new FrameDecoder();
assert.deepEqual(decoder.push(frame.subarray(0, 2)), []);
assert.deepEqual(decoder.push(frame.subarray(2))[0], { protocol: PROTOCOL, type: "hello", nonce: "n" });
console.log("NODE_SDK_OK");
