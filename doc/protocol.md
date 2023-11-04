# UMQ Protocol

## Message types

* **text message** - message can contain only text (encoding UTF-8)
* **binary message** - message contain any binary content (unlimited size)

### Message encoding

The UMQ protocol doesn't define the message encoding. It is handled by a protocol at
a lower layer. For example, when UMQ is implemented above WebSocket protocol, the messages are actually WebSocket messages 

## Protocol control

Text messages are only messages used to control the protocol. The binary messages contain only user payload

## Text message format

```
message = ['A' count separator] command message_id separator text_payload
command = '-'|'H'|'W'|'F'|'C'|'B'|'R'|'E'|'T'|'D'|'U'|'S'|'X'
message_id = digit36*
count = digit36*
digit36 = '0'-'9' | 'A'-'Z'
separator = ':'
text_payload = <arbitrary text>
```

Examples

* **RPC call message** - `C1AZ3:["method","arg1","arg2"]`
* **RPC call with two attachments** - `A3:C1AZ4:["method","arg1","arg2"]`

**NOTE**: The format of a payload is not specified by this document. The JSON is used as example of user payload

## Message ID

The message ID - message identifier - is Base36 encoded number. This number as various meaning for every message type. 

## Attachment count

Some messages can have attachments. Attachments are binary messages associated with the text message. The message specifies count of binary messages that follows this message. 
A command `A` is followed by count of attachments. The payload of this command is another command which must not be 'A'

```
A<count>:<command><id>:<payload>
<binary message>
<binary message>
...
```

Example

```
client: C121:Send me duck.png
server: A1:R121:Sending image/png
server: <binary content>
```


## Commands

### Command `H` - hello

This command is issued by a client, which connected a server. It initiates UMQ protocol.

The `message id` carries the protocol version.

The payload can carry any required information needed by the server to authorize the client

This message can have attachments

### Command `W` - welcome

This command is reply to command 'H'. The server accepts the connection. 

The `message id` carries the protocol version. The serve can lower this value.

The payload can carry any required information needed by the client to authorize the server

This message can have attachments

### Command `F` - Fatal

The command informs the other side about a fatal error. This is also a last command of the protocol, because sender is also required to close connection

The `message id` is not used (can be zero)

The payload contains description of the error in a format `<code> <message>`

This message can't have attachments

This message can be direct reply to message 'H'. However this message can appear anytime in the stream

```
F0:123 Authorization failed
F0:3 Protocol error
```

### Command `'-'` - Attachment error

Replaces attachment. This command is count as attachment, but it carries error in the attachment. Because attachments can be delivered asynchonously and the associated message was sent before the error was encoutered. This command replaces missing attachment

The `message id` is not used (can be zero)


This message can't have attachments

```
-:Upstream download error
```

### Command `C` - RPC call

Perform remote procedure call. Calls are always in form REQUEST-RESPONSE. This message represents the request. 

The `message id` can be any arbitrary number (but unique for this message). The response will have the same message id, which helps to assign responses to requests

The protocol expects that multiple RPC requests can be pending at the time

This message can have attachments

```
C5AX12:How are you?
```

### Command `R` - RPC result

Sent by RPC server as response to a RPC request. It indicates a successful call and carries a result

The `message id` must contain ID of RPC request

This message can have attachments

```
R5AX12:Fine
```

### Command `E` - RPC Exception

Sent by RPC server as respone to a RPC request. It indicates a failure and carries an error description

This message can't have attachments

```
client: C112:Brew coffee
server: E112:I'm teapot
```

### Command `B` - Callback

The callback is reversed call, when RPC server is able to "call" a client. The RPC client registers a number which represents the callback identifier. Then this identifier is
registered on RPC server. When RPC server wants to call a RPC client (perform a callback), it uses this message to perform RPC call

The message payload is separated by extra separator ':'. The first part of the payload contains a callback ID encoded as Base36 number, The rest of payload is argument of the call

The `message id` can be any arbitrary number (but unique for this message). The response will have the same message id, which helps to assign responses to requests

The response is sent as 'R' or 'E'

```
client: C13:I am ready, please send me a work as ID=4454
server: R13:accepted
server: B63:4454:here is a work: <work>. Send me result as ID=81A5
client: R63:accepted
client: B88:81A5:Work done: <result>
server: R88:Thank you!
```

This message can have attachments


### Command `T` - topic update

An update on given topic. The client can *subscribe* to a topic by sending an unique ID of the topic to a publisher (other side). When subscription is accepted, publisher send this message for every update on the topic

The `message id` contains ID of the topic

This message can have attachments


```
client: C41:Subscribe on 'ABC'. TopicID=9A8Z
server: R41:accepted
server: T9A8Z: update1
server: T9A8Z: update2
server: T9A8Z: update3
server: T9A8Z: update4
...
...

```


### Command `D` - topic close


Sent by publisher, when topic is closed (end of stream)

The `message id` contains ID of the topic

Payload is empty

This message can't have attachments


```
client: C41:Subscribe on 'ABC'. TopicID=9A8Z
server: R41:accepted
server: T9A8Z: update1
server: T9A8Z: update2
server: T9A8Z: update3
server: T9A8Z: update4
server: D9A8Z:
```


### Command `U` - unsubscribe

Sent by client to close topic prematurely (from client side). The server must respond with D if such topic exist. It is allowed to send this command to already closed topics, such message is ignored

The `message id` contains ID of the topic

Payload is empty

This message can't have attachments



```
client: C41:Subscribe on 'ABC'. TopicID=9A8Z
server: R41:accepted
server: T9A8Z: update1
server: T9A8Z: update2
client: U9A8Z:
server: T9A8Z: update3
client: U9A8Z:
server: D9A8Z:
```


### Command `S` - set attribute

Sets attribute on the other peer

The attribute consists of `attribute_name` and `attribute_value`. The payload carries `attribute_name=attribute_value`. There must not be spaces before and after '=' (or they are also part of name or value)

The `message id` is not used

```
S:key=value
```

This message can have attachments. These attachments are considered as part of the value


### Command `X` - unset attribute

Clears attribute on the other peer

The payload contains name of an attribute to clear

The `message id` is not used


```
X:key
```

