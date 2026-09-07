# Assistant Runtime

`AssistantRuntime` is the narrow, authenticated channel for an always-on
LlamaCode agent. It intentionally does not expose `ControlApi`: clients can
submit messages, inspect queued messages/events and receive idempotent replies,
but cannot invoke arbitrary `AppController` methods.

The HTTP listener is loopback by default. Binding it to a mesh interface is an
explicit deployment choice and still requires the bearer token. Telegram,
Discord or another transport can be implemented as an adapter that translates
its inbound/outbound messages to this contract.
