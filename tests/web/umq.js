

export const PeerError = {
    noError:0,
    unexpectedBinaryFrame:1,
    messageParseError:2,
    unknownMessageType:3,
    messageProcessingError:4,
    unsupportedVersion:5,
    unhandledException:6,
    methodNotFound:7,
    callbackIsNotRegistered:8


}

export const PeerMsgType =  {
    execution_error : '!',
    discover : '?',
    callback : 'C',
    exception : 'E',
    hello : 'H',
    method_call : 'M',
    result : 'R',
    var_set : 'S',
    topic_update : 'T',
    unsubscribe : 'U',
    welcome : 'W',
    var_unset : 'X',
    topic_close : 'Z'
};


///Exception can be thrown from the call. It can be also passed as result (to throw on other side) 
export class Exception extends Error {
    constructor(code, message) {
      let m = message === undefined?code:code + " " + message;
      super(m);
      this.name = this.constructor.name;
    }
    get_code() {
        return parseInt(Peer.split(" ".this.message)[0]);
    }
    get_message(){
        return Peer.split(" ".this.message)[1];
    }
}

///Execution error is mostly used as error happened before the execution reached the method
export class ExecutionError extends Error {
    constructor(message) {
      super(message)
      this.name = this.constructor.name;
    }
    get_code() {
        return parseInt(Peer.split(" ".this.message)[0]);
    }
    get_message(){
        return Peer.split(" ".this.message)[1];
    }
}

export class Request extends String {
    #method_name;
    #peer;
    constructor(text, method_name, peer) {
        super(text);
        this.#method_name = method_name;
        this.#peer = peer;                 
    }
    get_method_name() {
        return this.#method_name;
    }
    get_peer() {
        return this.#peer;
    }
}


export class Peer {
    
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
       this.#on_error = this.#ws.onerror = x => {
            e1(x);e2(x);
       };
        this.#p_disconnect.then(()=>this.#cleanup(),()=>this.#cleanup());
        this.#connected = true;
    }
    
    ///initializes peer, sends hello message to the other side, returns when handshake is done
    /**
    @param data optional message send to the server
     */
    async init(data) {
        if (!this.#p_welcome) {
           this.#p_welcome = new Promise((resolve, reject)=>{
                this.#welcome_done = resolve;
                this.#p_open.catch(r => reject(r)); 
                this.#p_disconnect.then(r => reject(r),r => reject(r));            
           });
           await this.#p_open;           
           this.#send_hello(Peer.version, data || "");
        }
       const out = await this.#p_welcome;
       this.#var_shadow = {};
       this.vars = new Proxy(this.#var_shadow, this.#proxy_handler());       
       return out;
    }   
    
    ///Waits for close peer, also caughts error (throws an exception)
    async wait_close() {
        await this.#p_disconnect;
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
        if (this.#connected) {
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
        } else {
            throw Peer.disconnect_exception();
        }
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

    ///Discover the other peer
    /**
    @param query discover query (can be empty)    
     */    
    async discover(query) {
        const x = await this.#create_request(id => {
            this.#send_discover(id, query || "");
        });
        if (x.substr(0, 1) == "D") {
            return x.substr(1);
        } else {
            let res = {};
            let list = x.split("\n");
            res.methods = list.filter(x_1 => x_1.substr(0, 1) == 'M').map(x_2 => x_2.substr(1));
            res.routes = list.filter(x_3 => x_3.substr(0, 1) == 'R').map(x_4 => x_4.substr(1));
            return res;
        }
    }
    
   
    async call(method, args) {
        return await this.#create_request(id=>{
            this.#send_method_call(id, method, args || "");
        });
    }

    async call_callback(name, args) {
        return await this.#create_request(id=>{
            this.#send_callback(id, name, args || "");
        });
    }

    
    ///Register callback
    /**
    @param fn function called as callback
    @return id of the callback - other side need to know this id to call the callback
    */  
    reg_callback(fn) {
        const id = (this.#req_next_id++)+"_cb";
        this.#callbacks[id] = fn;
        return id; 
        
    }
    
    unreg_callback(id) {
        delete this.#callbacks[id]
    }
    
    
    
    ///set and object with methods
    methods = {};
    
    ///variables shared by peer  (set by peer, you can read them)
    peer_vars = {}
    
    ///variables shared with peer  (you set the variables and peer can read them)
    vars = {}
     
    #create_request(send_fn) {
        return new Promise((ok, err)=>{
            if (this.#connected) {
                
                const id = this.#req_next_id++;
                const reqid = id.toString();
                
                this.#requests[reqid] = {ok:ok,err:err};
                send_fn(reqid);                
            } else {
                err(Peer.disconnect_exception());
            }
            
        })       
    }
               
           
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
                case PeerMsgType.callback:{
                            const [name,args] = Peer.split("\n", data);
                            this.#on_callback(id, name, args);
                          }                
                        
                        break;                
                case PeerMsgType.method_call: {
                            const [name,args] = Peer.split("\n", data);
                            this.#on_call(id, name, args);
                          }                
                          break;
                case PeerMsgType.result:
                          this.#on_result(id, data);
                          break;
                case PeerMsgType.exception: 
                          this.#on_exception(id, data);
                          break;
                case PeerMsgType.execution_error: 
                          this.#on_execute_error(id, data);
                          break;
                case PeerMsgType.discover: 
                          this.#on_discover(id, data);
                          break;
                case PeerMsgType.topic_update: 
                          this.#on_topic_update(id, data);
                          break;
                case PeerMsgType.unsubscribe:
                          this.#on_unsubscribe(id);
                          break;
                case PeerMsgType.topic_close: 
                          this.#on_topic_close(id);
                          break;
                case PeerMsgType.var_set:
                          this.#on_var_set(id, data);
                          break;
                case PeerMsgType.var_unset: 
                          this.#on_var_unset(id);
                          break;
                case PeerMsgType.welcome:
                          this.#on_welcome(id, data);                          
                          break;
    
            }
        } catch (e) {
            this.#send_node_error(PeerError.messageProcessingError);
        }
    }

    async #on_callback(id, name, data) {
        const s = this.#callbacks[name];
        if (!s) this.#send_execute_error(id, PeerError.callbackIsNotRegistered);
        delete this.#callbacks[name];
        try {
            this.#send_response(id, await s(new Request(data,"<callback>", this)));
        } catch (e) {
            this.#send_exception(id, PeerError.unhandledException, e.toString());
        }
    }

    async #on_call(id, name, args) {
        const m = this.methods[name];
        if (!m) {
            this.#send_execute_error(id, PeerError.methodNotFound);            
        } else {
            try {
                this.#send_response(id, await m(new Request(args, name, this)));
            } catch (e) {                              
                this.#send_exception(id, PeerError.unhandledException, e.toString());
            }
        }        
    }

    #on_result(id, data) {
        const r = this.#pop_request(id);
        if (r) {
            r.ok(data);
        }
    }

    #on_exception(id, data) {
        const e = new Exception(data);
        if (!id) this.#on_error(data)
        else {
            const r = this.#pop_request(id);
            if (r) {
                r.err(e);
            }
        }
    }

    
    #on_execute_error(id, data) {
        const e = new ExecutionError(data);
        if (!id) this.#on_error(e)
        else {
            const r = this.#pop_request(id);
            if (r) {
                r.err(e);
            }
        }
        
    }


    #on_topic_update(id,data) {
        const s = this.#subscriptions[id];
        if (!s || !s(data)) {
            delete this.#subscriptions[id];
            this.#send_unsubscribe(id);
        }
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
        this.peer_vars[id] = data;
    }

    #on_var_unset(id) {
        delete this.peer_vars[id];
    }
    #on_discover(id,query) {
        if (!query) {
            var res = "";
            for (var i in this.methods) {
                res = res + "M"+i+"\n";
            }    
            this.#send_result(id, res);
        } else if (this.methods[query]) {
            var doc = this.methods[query].doc;
            this.#send_result(id, doc);
        } else {
            this.#send_exception(id, PeerError.methodNotFound, Peer.error_to_string(PeerError.methodNotFound));
        }
    }
    
    #send_unsubscribe(id) {
        this.#ws.send(PeerMsgType.unsubscribe+id);
    }
    
    #send_node_error(error) {
        var err = error.toString()+" "+Peer.error_to_string(error);
        this.#ws.send(PeerMsgType.exception+"\n"+err);        
        this.#ws.close();        
        this.#on_error(new Exception(error,Peer.error_to_string(error)));
    }
    
    #send_execute_error(id, errcode) {
        this.#ws.send(PeerMsgType.execution_error+id+"\n"+errcode+" "+Peer.error_to_string(errcode));
    }
    
    #send_discover(id, query) {
        this.#ws.send(PeerMsgType.discover+id+"\n"+query);
    }
    
    #send_exception(id, arg1, arg2) {
        if (arg2 === undefined) {
            this.#ws.send(PeerMsgType.exception+id+"\n"+arg1);
        } else {
            this.#ws.send(PeerMsgType.exception+id+"\n"+arg1+" "+arg2);
        }        
    }
    #send_result(id, data) {
        this.#ws.send(PeerMsgType.result+id+"\n"+data);       
    }
    #send_hello(id, data) {
        this.#ws.send(PeerMsgType.hello+id+"\n"+data);
    }
    #send_topic_close(id) {
        this.#ws.send(PeerMsgType.topic_close+id);
    }
    #send_topic_update(id, data) {
        this.#ws.send(PeerMsgType.topic_update+id+"\n"+data);
        
    }
    #send_method_call(id, method, args) {
        this.#ws.send(PeerMsgType.method_call+id+"\n"+method+"\n"+args);
    }
    #send_callback(id,name, args) {
        this.#ws.send(PeerMsgType.callback+id+"\n"+name+"\n"+args);
    }
        
    #cleanup() {        
        for (var n in this.#requests) {
            this.#requests[n].err(Peer.disconnect_exception());
        }        
        this.#topics = {};
        this.#subscriptions = {};
        this.#requests = {};
        this.#connected = false;
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
    #send_response(id, r) {
        if (r === undefined) this.#send_result(id, "");
        else if (r instanceof ExecutionError) this.#send_execute_error(id, r.message);
        else if (r instanceof Exception) this.#send_exception(id, r.message);
        else this.#send_result(id, r.toString());
    }
    send_var_set(name, val) {
        this.#ws.send(PeerMsgType.var_set+name+"\n"+val);
    }
    send_var_unset(name) {
        this.#ws.send(PeerMsgType.var_unset+name);
    }
    
    #proxy_handler() {
        var me = this;
        return {
            set(o,k,v) {
                if (o[k] !== v) {
                    me.send_var_set(k,v.toString());
                }
                o[k] = v;
                return true;
            },
            deleteProperty (o,k) {
                me.send_var_unset(k);
                delete o[k];
                return true;                
            }
        }
    }
    
    #on_welcome(version, data) {
        if (version != Peer.version) {
            this.#send_node_error(PeerError.unsupportedVersion);
        } else {
            this.#welcome_done(data);
        }
    }

    #topics = {}; //topic map with unsubscribe function
    #subscriptions = {}; //active subscriptions
    #requests = {}; //requests
    #callbacks = {}; //callbacks
    #req_next_id = 0;  

    #welcome_done; //function to be called when welcome arrives 
    #on_error;  //function to be called on peer error
    #connected = false;
  
    #var_shadow;
  
    #ws;      //websocket
    #p_open;  //open promise - triggered once the websocket connection is opened
    #p_welcome;  //welcome promise - triggered after welcome arrives
    #p_disconnect; //disconnect promise
 
    
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
            case PeerError.methodNotFound: return "Method not found";
            case PeerError.callbackIsNotRegistered: return "Callback not registered";
            default: return "Undetermined error";
        }
    }
    static disconnect_exception() {
        return new ExecutionError("-1 Peer disconnected");
    }

};




