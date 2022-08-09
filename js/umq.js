

const PeerError = {
    noError:0,
    unexpectedBinaryFrame:1,
    messageParseError:2,
    unknownMessageType:3,
    messageProcessingError:4,
    unsupportedVersion:5,
    unhandledException:6,
    requestLost:7,

}

///Exception can be thrown from the call. It can be also passed as result (to throw on other side) 
class Exception extends Error {
    constructor(code, message) {
      let m = status + " " + message;
      super(m);
      this.name = this.constructor.name;
    }
}

///Execution error is mostly used as error happened before the execution reached the method
class ExecutionError extends Error {
    constructor(message) {
      super(message)
      this.name = this.constructor.name;
    }
}


///Request is type of result, which must be respond
/**
This can happen, when the method need some information from the caller. 
It can be some sort of additional information, so request is not finished. The caller must
supply additional informations and then wait for response. This can happen repeatedly until
the result is returned. 

Other use is to 3-way request, which created by caller, responded by callee but caller then
must pass final information to callee and then request end. 

There are two method to achieve this - send_reply and cont_request

 */
class Request {
    constructor(data, callback) {
        this.data = data;
        this.callback = callback;
    }
    toString() {
        return this.message();
    }
    ///Sends reply to this request and finish the request
    send_reply(r) {
        this.callnack(r);
    }
    ///Sends reply to this request and continue the request, so method returns result
    /**
      Note that method also can return additional Request object.
     */
    async cont_request_(r, info) {
        return this.callback(r, info || true);
    }
    
    send_exception(e) {
        this.callback(new Exception(e));
    }
    
}


class Peer {
    
    constructor(url) {               
       
       let e1, e2;
       
       this.#ws = new WebSocket(url);
       this.#p_disconnect = new Promise((resolve,reject)=>{
            this.#ws.onclose = resolve;
            e1 = reject; 
       });
       this.#p_open = new Promise((resolve, reject)=>{
            this.#ws.onopen = resolve;
            e2 = reject;     
       });
       this.#ws.onmessage = msg => this.#receive_msg(msg);
       this.#ws.onclose = x => this.#ondisconnect(x);
       this.#on_error = this.#ws.onerror = x => {
            e1(x);e2(x);
       };
       this.#openp = new Promise(ok => {this.#ws.onopen = ok;});
       this.#disconnectp = new Promise((ok,error)=> {
            this.#ondisconnect = ok;
            this.#onerror = error;   
        });     
        this.#p_disconnect.then(()=>this.#cleanup,()=>this.#cleanup);
    }
    
    ///initializes peer, sends hello message to the other side, returns when handshake is done
    /**
    @param data optional message send to the server
     */
    async init(data) {
        if (!this.#p_welcome) {
           this.#p_welcome = new Promise((resolve, reject)=>{
                this.#on_welcome = resolve;
                this.#p_open.catch(r => reject(r));             
           });
           await this.#p_open;           
           this.#send_hello(Peer.version, data || "");
        }
        return await this.#p_welcome;
    }   
    
    ///Waits for close peer, also caughts error (throws an exception)
    async wait_close() {
        await this.#disconnectp;
    }
    
    subscribe(topic, callback) {
        this.#subscriptions[topic] = callback;
    }
    
    ///Initiates publishing of given topic
    /**
    @param topic topic id
    @return Returns a function, which can be used to publish the topic (send topic update)
      The function then retuns true to continue publish or false to stop publishing
      The function object also contains onunsubscribe variable, which can be
      filled with a function, which is called when peer requests to unsubscribe. This
      allows to remove peer from the publisher sooner than it is detected by
      failed publishing
    */
    start_publish(topic) {
        let fn = msg => {
            const allow = topic in this.#subscriptions;
            if (allow) {
                if (msg === undefined || msg === null) {
                    this.#send_topic_close(topic);
                } else {
                    this.#send_topic_update(topic, msg);
                }
                return true;
            } else {
                return false;
            }
        }
        this.#topics[topic] = fn;
        fn.onunsubscribe = null;
        return fn;
    }

        
    unsubscribe(topic) {
        if (topic in this.#subscriptions) {
            this.#subscriptions[topic](null);
            delete this.#subscriptions[topic];
            this.#send_unsubscribe(topic);
            return true;
        } else {
            return false;
        }
    }
    
    call(method, args, info_cb) {
        return new Promise((ok, error)=>{
            const id = this.#req_next_id++;
            const reqid = id.toString();
            
            this.#requests[reqid] = [ok,error, info_cb];
            this.#send_call(reqid, method, args);
        });

    }
    ///set and object with methods
    methods = {};
    
    ///variables set by peer
    peer_variables = {} 
           
           
    #receive_msg(msg) {
        if (typeof msg.data == "string") this.#parse_msg(msg.data);
    }
        
    #parse_msg(msg) {
        const [topic,data] = Peer.split("\n",msg);
        const type = topic.substring(0,1);
        const id = topic.substring(1);
        try {
            switch (type) {
                default:
                    this.#send_node_error(PeerError.unknownMessageType);;
                    break;
                case 'C': {
                        const [name,args] = Peer.split("\n", data);                
                        try {
                            if (!this.#on_call(id, name, args)) {
                              this.#send_execute_error(id, "Method not found");
                            }
                        } catch (e) {
                            this.#send_exception(id, PeerError.unhandledException, e.toString());
                        }
                      }
                          break;
                case 'P': try {
                              this.#on_request_continue(id, data);
                          } catch (e) {
                              this.#send_exception(id, PeerError.unhandledException, e.toString());
                          }
                          break;
                case 'I': try {
                              this.#on_request_info(id, data);
                          } catch (e) {
                              console.warn("Peer info exception", e);
                          }
                          break;
                case 'R': this.#on_result(id, data);
                          break;
                case 'E': this.#on_exception(id, data);
                          break;
                case '?': this.#on_execute_error(id, data);
                          break;
                case 'T': if (!this.#on_topic_update(id, data)) this.#send_unsubscribe(id);
                          break;
                case 'U': this.#on_unsubscribe(id);
                          break;
                case 'N': this.#on_topic_close(id);
                          break;
                case 'S': this.#on_var_set(id, data);
                          break;
                case 'X': this.#on_var_unset(id);
                          break;
                case 'W': if (id != Peer.version) {
                              this.#send_node_error(PeerError.unsupportedVersion);
                          } else {
                              this.#on_welcome(data);
                          }
                          break;
    
            }
        } catch (e) {
            this.#send_node_error(PeerError.messageProcessingError);
        }
    }

    
    #on_topic_update(id,data) {
        const s = this.#subscriptions[id];
        if (!s || !s(data)) {
            delete this.#subscriptions[id];
            this.#send_unsubscribe(id);
        }
    }
    
    #on_execute_error(id, data) {
        const e = new ExecutionError(data);
        if (!id) this.#on_error(e)
        else {
            const r = this.#pop_request(id);
            if (r) {
                r[1](e);
            }
        }
        
    }
    
    #on_exception(id, data) {
        const e = new Exception(data);
        if (!id) this.#on_error(data)
        else {
            const r = this.#pop_request(id);
            if (r) {
                r[1](e);
            }
        }
    }
    
    #on_result(id, data) {
        const r = this.#pop_request(id);
        if (r) {
            r[0](data);
        }
    }
    
    async #on_call(id, name, args) {
        const m = this.methods[id];
        if (!m) {
            this.#send_execute_error(id, "Method not found");            
        } else {
            try {
                const r = await m(args, name, (args, info)=>{
                    this.#continue_request(id, args, info);
                });
                this.#send_response(id, r);
            } catch (e) {                              
                this.#send_exception(PeerError.unhandledException + " " + e.toString());
            }
        }
        
    }
    #on_request_continue(id, data) {
        if (!this.#finalizer) {
            this.#finalizer = new FinalizerRegistry(x=>this.#close_request(x));
        }
        const r = this.#pop_request(id);
        if (r) {
            try {
                let rqhndl = async (data, info)=>{
                  if (!info) {
                    this.#send_response(id, data);
                  } else {
                     return await (new Promise((ok,err)=>{
                        this.#requests[id] = [ok, err, typeof info == "function"?info:null];
                     }));
                  }                
                };
                this.#finalizer.register(rqhndl,id);
                r[0](new Request(data, rqhndl));
            } catch (e) {
                this.#send_exception(PeerError.unhandledException+" " + e.toString());
            }
        } else {
            this.#send_exception(PeerError.requestLost+" "+Peer.error_to_string(PeerError.requestLost));
        }
    }
    #on_request_info(id, data) {
        const n = this.#requests[id];
        if (n && n[2]) n[2](data);
    }
    
    #on_unsubscribe(id) {
        const s = this.#topics[id];
        delete this.#topics[id];
        if (s && s.onunsubscribe) {          
            s.onunsubscribe();            
        }
    }
    
    #on_topic_close(id) {
        const n = this.#subscriptions[id];
        if (n) {
            n(null);
            delete this.#subscriptions[n];
        }
    }
    
    #on_var_set(id, data) {
        this.peer_variables[id] = data;
    }

    #on_var_unset(id) {
        delete this.peer_variables;
    }
    
    #send_unsubscribe(id) {
        this.#ws.send("U"+id);
    }
    
    #send_node_error(error) {
        this.#ws.send("E\n"+error.toString()+" "+Peer.error_to_string(error));
        this.#ws.close();        
    }
    
    #send_execute_error(id, error) {
        this.#ws.send("?"+id+"\n"+error);
    }
    
    #send_exception(id, error) {
        this.#ws.send("E"+id+"\n"+error);
        
    }
    #send_result(id, data) {
        this.#ws.send("R"+id+"\n"+data);       
    }
    #send_hello(id, data) {
        this.#ws.send("H"+id+"\n"+data);
    }
    #send_topic_close(id) {
        this.#ws.send("N"+id);
    }
    #send_topic_update(id, data) {
        this.#ws.send("T"+id+"\n"+data);
        
    }
    #send_call(method, args) {
        this.#ws.send("C"+id+"\n"+method+"\n"+args);
    }
    
    #cleanup() {
        this.#topics = {}
        this.#subscriptions = {}        
    }
    
    #pop_request(id) {
        const r = this.#requests[id];
        if (r) {
            delete this.#requests[id];
            return r;
        } else {
            return null;
        }
    }
    #close_request(id) {
        this.#send_result(id, "");
    }
    #send_response(id, r) {
        if (r instanceof ExecutionError) this.#send_execute_error(id, r.message);
        else if (r instanceof Exception) this.#send_exception(id, r.message);
        else this.#send_result(id, r.toString());
    }

    #topics = {}; //topic map with unsubscribe function
    #subscriptions = {}; //active subscriptions
    #requests = {}; //requests
    #req_next_id = 0;  

    #on_welcome; //function to be called when welcome arrives 
    #on_error;  //function to be called on peer error
  
    #ws;      //websocket
    #p_open;  //open promise - triggered once the websocket connection is opened
    #p_welcome;  //welcome promise - triggered after welcome arrives
    #p_disconnect; //disconnect promise
    #finalizer;    //finalizer registry    
    
    static create_ws_path(loc /*window.location*/, path /*relative path*/) {
        let new_uri;
        if (loc.protocol === "https:") {
            new_uri = "wss:";
        } else {
            new_uri = "ws:";
        }
        new_uri += "//" + loc.host;
        new_uri += loc.pathname + path;
        return new_uri;
    } 

    static split(c,s) {
        const p = s.indexOf(c);
        if (p == -1) return [s];
        else return [s.substring(0,p),s.substring(p+1)];
    }
    
    static version = "1.0.0";


    static error_to_string(err) {
        switch(err) {
            case PeerError.noError: return "No error";
            case PeerError.unexpectedBinaryFrame: return "Unexpected binary frame";
            case PeerError.messageParseError: return "Message parse error";
            case PeerError.unknownMessageType: return "Unknown message type";
            case PeerError.messageProcessingError: return "Internal node error while processing a message";
            case PeerError.unsupportedVersion: return "Unsupported version";
            case PeerError.unhandledException: return "Unhandled exception";
            case PeerError.requestLost: return "Request lost"
            default: return "Undetermined error";
        }
    }

};




