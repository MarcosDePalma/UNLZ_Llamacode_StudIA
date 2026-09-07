export const PROTOCOL: "llamacode-worker-v1";
export const DEFAULT_MAX_FRAME_BYTES: number;

export type JsonObject = Record<string, unknown>;

export class FrameDecoder {
  constructor(maxFrameBytes?: number);
  push(chunk: Uint8Array | ArrayBuffer | string): JsonObject[];
}

export function encodeFrame(body: JsonObject, maxFrameBytes?: number): Buffer;

export class CapabilityError extends Error {
  readonly name: "CapabilityError";
  readonly code: string;
  readonly capability: string;
  constructor(code: string, message: string, capability?: string);
}

export interface CapabilityGrant {
  name?: string;
  handle?: string;
  reason?: string;
  generation?: number;
  granted?: boolean;
}

export interface CapabilitySnapshot {
  generation?: number;
  grants?: Record<string, CapabilityGrant>;
}

export class CapabilityHandle {
  readonly name: string;
  call(operation: string, payload?: JsonObject): Promise<JsonObject>;
}

export class CapabilityBroker {
  constructor(runtime: WorkerRuntime, snapshot?: CapabilitySnapshot);
  readonly generation: number;
  names(): string[];
  has(name: string): boolean;
  require(name: string): CapabilityHandle;
}

export interface CallContext {
  readonly callId: string;
  readonly signal: AbortSignal;
  readonly cancelled: boolean;
  readonly capabilities: CapabilityBroker;
}

export type WorkerHandler =
  (payload: JsonObject, context: CallContext) => unknown | Promise<unknown>;

export interface WorkerRuntimeOptions {
  handlers?: Record<string, WorkerHandler>;
  maxFrameBytes?: number;
}

export interface WorkerRunOptions {
  input?: NodeJS.ReadableStream;
  output?: NodeJS.WritableStream;
}

export class WorkerRuntime {
  constructor(options?: WorkerRuntimeOptions);
  readonly authenticated: boolean;
  readonly capabilities: CapabilityBroker;
  run(options?: WorkerRunOptions): Promise<void>;
}

export function createWorker(options?: WorkerRuntimeOptions): WorkerRuntime;
