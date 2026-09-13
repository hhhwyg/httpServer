(() => {
  "use strict";
  const MAX_MESSAGE_LENGTH=4096,MAX_CACHED_MESSAGES=1000;
  const keys={token:"linya.token",username:"linya.username",room:"linya.room"};
  const state={token:localStorage.getItem(keys.token)||"",username:localStorage.getItem(keys.username)||"",currentRoomId:localStorage.getItem(keys.room)||"",rooms:[],messages:new Map(),unread:new Map(),unreadIds:new Map(),readThrough:new Map(),pending:new Map(),drafts:new Map(),historyCursors:new Map(),historyInitialized:new Set(),historyRequests:new Map(),roomVersions:new Map(),loadingRooms:new Set(),subscriptions:new Map(),subscribedRooms:new Set(),subscriptionQueue:[],renderedRoomId:"",sessionVersion:0,composing:false,roomsLoading:false,ws:null,reconnectTimer:null,reconnectDelay:1000,shouldReconnect:true,authMode:"login",roomFilter:"all"};
  const $=id=>document.getElementById(id), authView=$("auth-view"), appView=$("app-view"), authForm=$("auth-form"), authMessage=$("auth-message"), authSubmit=$("auth-submit"), usernameInput=$("username"), passwordInput=$("password"), roomList=$("room-list"), messageList=$("message-list"), messageInput=$("message-input"), sendButton=$("send-button"), charCount=$("char-count"), roomDialog=$("room-dialog"), roomForm=$("room-form"), roomNameInput=$("room-name-input"), roomMessage=$("room-message"), joinRoomDialog=$("join-room-dialog"), joinRoomForm=$("join-room-form"), joinRoomIdInput=$("join-room-id-input"), joinRoomMessage=$("join-room-message");
  class ApiError extends Error{constructor(message,status,code=""){super(message);this.status=status;this.code=code}}
  const text=(value)=>String(value==null?"":value);
  const messageBytes=value=>new TextEncoder().encode(value).length;
  const identifier=()=>globalThis.crypto&&crypto.randomUUID?crypto.randomUUID():`${Date.now()}-${Math.random().toString(36).slice(2)}`;
  const initials=(value)=>{const valueText=text(value).trim();return valueText?valueText.slice(0,1).toUpperCase():"?"};
  function tone(value){let hash=0;for(const character of text(value))hash=(hash*31+character.codePointAt(0))>>>0;return String(hash%4)}
  function setText(element,value){element.textContent=text(value)}
  function formMessage(element,message,success=false){setText(element,message);element.classList.toggle("success",success)}
  function toast(message,type="info"){const node=document.createElement("div");node.className=`toast ${type}`;node.setAttribute("role",type==="error"?"alert":"status");setText(node,message);$("toast-region").appendChild(node);setTimeout(()=>node.remove(),4200)}
  function errorText(error,fallback){return error instanceof ApiError&&error.message?error.message:fallback}
  function renderSkeletons(target,className,count){const stack=document.createElement("div");stack.className="loading-stack";for(let index=0;index<count;index+=1){const item=document.createElement("div");item.className=className;stack.appendChild(item)}target.appendChild(stack)}
  function updatePageTitle(){const total=[...state.unread.values()].reduce((sum,count)=>sum+count,0);document.title=total?`（${total>99?"99+":total}）澄语协作 | 实时沟通平台`:"澄语协作 | 实时沟通平台"}
  async function api(path,options={}){const version=state.sessionVersion,headers=new Headers(options.headers||{});if(options.body&&!headers.has("Content-Type"))headers.set("Content-Type","application/json");if(state.token)headers.set("Authorization",`Bearer ${state.token}`);const response=await fetch(path,{...options,headers}),raw=await response.text();if(version!==state.sessionVersion)throw new ApiError("会话已切换",0,"stale_session");let data={};try{data=raw?JSON.parse(raw):{}}catch(_){data={msg:raw}}if(!response.ok)throw new ApiError(data.error?.message||data.msg||`请求失败（${response.status}）`,response.status,data.error?.code||"");return data}
  function validate(username,password){if(!/^[A-Za-z0-9_-]{3,64}$/.test(username))return"用户名需为 3-64 位字母、数字、_ 或 -";if(password.length<8||password.length>128)return"密码长度需为 8-128 位";return""}
  function setPasswordVisible(visible){passwordInput.type=visible?"text":"password";const button=$("password-toggle"),label=visible?"隐藏密码":"显示密码";button.title=label;button.setAttribute("aria-label",label);button.setAttribute("aria-pressed",String(visible));button.firstElementChild.className=`icon ${visible?"icon-eye-off":"icon-eye"}`}
  function setAuthMode(mode){state.authMode=mode;const login=mode==="login";$("login-tab").classList.toggle("active",login);$("register-tab").classList.toggle("active",!login);$("login-tab").setAttribute("aria-selected",String(login));$("register-tab").setAttribute("aria-selected",String(!login));setText($("auth-title"),login?"欢迎回来":"创建账户");setText($("auth-caption"),login?"登录你的账户":"加入澄语协作");setText(authSubmit,login?"登录":"注册");passwordInput.autocomplete=login?"current-password":"new-password";setPasswordVisible(false);formMessage(authMessage,"")}
  function authBusy(busy){authSubmit.disabled=busy;usernameInput.disabled=busy;passwordInput.disabled=busy;setText(authSubmit,busy?"处理中…":state.authMode==="login"?"登录":"注册")}
  function saveSession(username,token){state.username=username;state.token=token;localStorage.setItem(keys.username,username);localStorage.setItem(keys.token,token)}
  function clearSession(){state.sessionVersion+=1;state.token="";state.username="";state.currentRoomId="";for(const pending of state.pending.values())clearTimeout(pending.timer);state.pending.clear();state.messages.clear();state.rooms=[];state.unread.clear();state.unreadIds.clear();state.readThrough.clear();state.drafts.clear();state.historyCursors.clear();state.historyInitialized.clear();state.historyRequests.clear();state.roomVersions.clear();state.loadingRooms.clear();state.subscriptions.clear();state.subscribedRooms.clear();state.subscriptionQueue=[];state.composing=false;state.roomFilter="all";$("room-search").value="";messageInput.value="";resizeInput();updatePageTitle();localStorage.removeItem(keys.token);localStorage.removeItem(keys.username);localStorage.removeItem(keys.room)}
  async function submitAuth(event){event.preventDefault();const username=usernameInput.value.trim(),password=passwordInput.value,validation=validate(username,password);if(validation){formMessage(authMessage,validation);return}authBusy(true);try{const endpoint=state.authMode==="login"?"/login":"/register",data=await api(endpoint,{method:"POST",body:JSON.stringify({username,password})});if(state.authMode==="register"){setAuthMode("login");formMessage(authMessage,"注册成功，请使用新账户登录。",true);passwordInput.value="";passwordInput.focus();return}if(!data.token)throw new ApiError("服务器未返回登录令牌",500);saveSession(username,data.token);await enterChat()}catch(error){if(error.status===401)formMessage(authMessage,"用户名或密码错误");else if(error.status===503)formMessage(authMessage,"登录服务暂时不可用，请稍后重试");else formMessage(authMessage,errorText(error,"请求失败，请稍后重试"))}finally{authBusy(false)}}
  async function enterChat(){const version=state.sessionVersion;state.shouldReconnect=true;authView.hidden=true;appView.hidden=false;setText($("user-name"),state.username);setText($("user-avatar"),initials(state.username));updateConnection("connecting");try{await fetchRooms();if(version===state.sessionVersion)connectWebSocket()}catch(error){if(version!==state.sessionVersion)return;if(error.status===401){clearSession();showAuth("登录已失效，请重新登录");return}toast(errorText(error,"房间列表加载失败"),"error");updateConnection("offline")}}
  function showAuth(message="",success=false){state.shouldReconnect=false;clearTimeout(state.reconnectTimer);if(state.ws){state.ws.close();state.ws=null}toggleMobile(false);authView.hidden=false;appView.hidden=true;formMessage(authMessage,message,success);usernameInput.focus()}
  async function fetchRooms(){const version=state.sessionVersion;state.roomsLoading=true;renderRooms();try{let data=await api("/api/v1/rooms");state.rooms=Array.isArray(data.data?.rooms)?data.data.rooms.map(room=>({id:text(room.id),name:text(room.name||"未命名房间")})):[];if(!state.rooms.length){await api("/api/v1/rooms",{method:"POST",body:JSON.stringify({name:"综合大厅"})});data=await api("/api/v1/rooms");state.rooms=Array.isArray(data.data?.rooms)?data.data.rooms.map(room=>({id:text(room.id),name:text(room.name||"未命名房间")})):[]}if(!state.rooms.some(room=>room.id===state.currentRoomId)){state.currentRoomId=state.rooms[0]?state.rooms[0].id:"";messageInput.value=state.drafts.get(state.currentRoomId)||"";markRoomRead(state.currentRoomId);resizeInput()}if(state.currentRoomId)localStorage.setItem(keys.room,state.currentRoomId)}finally{if(version===state.sessionVersion){state.roomsLoading=false;renderRooms();renderCurrentRoom()}}if(version===state.sessionVersion&&state.currentRoomId)await loadHistory(state.currentRoomId)}
  function dateKey(date){return `${date.getFullYear()}-${date.getMonth()}-${date.getDate()}`}
  function displayDate(date){
    const today=new Date(),yesterday=new Date(today);
    yesterday.setDate(today.getDate()-1);
    if(dateKey(date)===dateKey(today))return "今天";
    if(dateKey(date)===dateKey(yesterday))return "昨天";
    return date.toLocaleDateString("zh-CN",{year:date.getFullYear()===today.getFullYear()?undefined:"numeric",month:"long",day:"numeric"});
  }
  function roomTime(date){return dateKey(date)===dateKey(new Date())?date.toLocaleTimeString("zh-CN",{hour:"2-digit",minute:"2-digit",hour12:false}):displayDate(date)}
  function advanceReadThrough(roomId,id){
    if(!/^[0-9]+$/.test(text(id)))return;
    if(BigInt(id)>BigInt(state.readThrough.get(roomId)||"0"))state.readThrough.set(roomId,text(id));
  }
  function markRoomRead(roomId){
    for(const item of state.messages.get(roomId)||[])advanceReadThrough(roomId,item.id);
    state.unread.delete(roomId);state.unreadIds.delete(roomId);
  }
  function trackUnread(roomId,item){
    if(roomId===state.currentRoomId){advanceReadThrough(roomId,item.messageId);state.unread.delete(roomId);state.unreadIds.delete(roomId);return}
    if(item.senderId===state.username||!/^[0-9]+$/.test(text(item.messageId))||BigInt(item.messageId)<=BigInt(state.readThrough.get(roomId)||"0"))return;
    const ids=state.unreadIds.get(roomId)||new Set();
    if(ids.has(text(item.messageId))||ids.size>=100)return;
    ids.add(text(item.messageId));state.unreadIds.set(roomId,ids);state.unread.set(roomId,ids.size);
  }
  function markHistoryBaseline(roomId,id){
    let through=BigInt(id);
    for(const unreadId of state.unreadIds.get(roomId)||[])if(BigInt(unreadId)<=through)through=BigInt(unreadId)-1n;
    advanceReadThrough(roomId,through.toString());
  }
  function renderRooms(){
    setText($("room-count"),state.rooms.length);
    const unreadRooms=state.rooms.filter(room=>(state.unread.get(room.id)||0)>0).length;
    setText($("unread-room-count"),unreadRooms);
    $("unread-room-count").hidden=!unreadRooms;
    for(const mode of ["all","unread"]){
      const tab=$(mode+"-rooms-filter"),active=state.roomFilter===mode;
      tab.classList.toggle("active",active);tab.setAttribute("aria-selected",String(active));tab.tabIndex=active?0:-1;
    }
    roomList.setAttribute("aria-busy",String(state.roomsLoading));
    const query=$("room-search").value.trim().toLowerCase();
    roomList.replaceChildren();
    if(state.roomsLoading){renderSkeletons(roomList,"room-skeleton",5);return}
    const rooms=state.rooms.filter(room=>room.name.toLowerCase().includes(query)&&(state.roomFilter!=="unread"||(state.unread.get(room.id)||0)>0));
    if(!rooms.length){
      const empty=document.createElement("div"),mark=document.createElement("span"),title=document.createElement("strong");
      empty.className="empty-state";mark.className="icon icon-brand";
      setText(title,query?"没有匹配的房间":state.roomFilter==="unread"?"没有未读消息":"还没有房间");
      empty.append(mark,title);roomList.appendChild(empty);updatePageTitle();return;
    }
    rooms.forEach(room=>{
      const button=document.createElement("button"),icon=document.createElement("span"),copy=document.createElement("span"),heading=document.createElement("span"),name=document.createElement("span"),meta=document.createElement("span");
      const count=state.unread.get(room.id)||0,list=state.messages.get(room.id)||[],latest=[...list].reverse().find(item=>item.kind!=="system"),draft=state.drafts.get(room.id)||"";
      button.className=`room-item ${room.id===state.currentRoomId?"active":""}`;button.type="button";
      button.title=room.name;button.dataset.roomId=room.id;button.dataset.tone=tone(room.id);button.setAttribute("aria-current",room.id===state.currentRoomId?"true":"false");
      button.addEventListener("click",()=>switchRoom(room.id));
      icon.className="room-icon";setText(icon,initials(room.name));
      copy.className="room-copy";heading.className="room-title-row";name.className="room-name";meta.className="room-meta";
      setText(name,room.name);heading.appendChild(name);
      if(latest){const time=document.createElement("time");time.className="room-time";time.dateTime=latest.time.toISOString();setText(time,roomTime(latest.time));heading.appendChild(time)}
      if(draft.trim()){
        const label=document.createElement("span");label.className="draft-label";setText(label,"草稿");
        meta.append(label,document.createTextNode(" "+draft.replace(/\s+/g," ")));
      }else setText(meta,latest?`${latest.sender===state.username?"你":latest.sender}: ${latest.content.replace(/\s+/g," ")}`:`房间 ID ${room.id}`);
      copy.append(heading,meta);button.append(icon,copy);
      if(count){const unread=document.createElement("span");unread.className="room-unread";unread.setAttribute("aria-label",count>99?"99 条以上未读消息":`${count} 条未读消息`);setText(unread,count>99?"99+":count);button.appendChild(unread)}
      roomList.appendChild(button);
    });
    updatePageTitle();
  }
  function renderCurrentRoom(){const room=state.rooms.find(item=>item.id===state.currentRoomId);setText($("current-room-icon"),room?initials(room.name):"?");setText($("current-room-name"),room?room.name:"选择一个房间");$("current-room-name").title=room?room.name:"";setText($("current-room-meta"),room?`房间 ID ${room.id}`:"");setText($("composer-room"),room?room.name:"");$("leave-room-button").disabled=!room;$("copy-room-button").disabled=!room;renderMessages();updateComposer()}
  async function switchRoom(roomId){const room=state.rooms.find(item=>item.id===text(roomId));if(!room)return;if(state.currentRoomId)state.drafts.set(state.currentRoomId,messageInput.value);state.currentRoomId=room.id;messageInput.value=state.drafts.get(room.id)||"";resizeInput();markRoomRead(room.id);localStorage.setItem(keys.room,room.id);renderRooms();renderCurrentRoom();toggleMobile(false);subscribeRoom(room.id);await loadHistory(room.id)}
  function latestStoredId(roomId){
    const list=state.messages.get(roomId)||[];
    for(let index=list.length-1;index>=0;index-=1)if(/^[0-9]+$/.test(list[index].id))return list[index].id;
    return "0";
  }
  function sortMessageList(list){
    list.sort((left,right)=>{
      const leftStored=/^[0-9]+$/.test(left.id)||left.kind==="system",rightStored=/^[0-9]+$/.test(right.id)||right.kind==="system";
      if(leftStored&&rightStored){
        const a=BigInt(left.kind==="system"?left.anchorId||"0":left.id),b=BigInt(right.kind==="system"?right.anchorId||"0":right.id);
        if(a!==b)return a<b?-1:1;
        if((left.kind==="system")!==(right.kind==="system"))return left.kind==="system"?1:-1;
      }else if(leftStored!==rightStored)return leftStored?-1:1;
      return left.time-right.time;
    });
  }
  function remember(roomId,message){if(!state.messages.has(roomId))state.messages.set(roomId,[]);const list=state.messages.get(roomId),existing=list.find(item=>item.id===message.id||(message.sender===state.username&&item.sender===state.username&&message.clientMessageId&&item.clientMessageId===message.clientMessageId));if(existing){Object.assign(existing,message);sortMessageList(list);return{message:existing,inserted:false}}list.push(message);sortMessageList(list);while(list.length>MAX_CACHED_MESSAGES){const index=list.findIndex(item=>item.status!=="sending"&&item.status!=="failed");if(index<0)break;list.splice(index,1)}return{message,inserted:true}}
  function addMessage(roomId,content,kind,sender,options={}){if(!roomId||!content)return null;const remembered=remember(roomId,{id:options.id||identifier(),anchorId:kind==="system"?latestStoredId(roomId):"",clientMessageId:options.clientMessageId||"",content:text(content),kind,sender:sender||(kind==="me"?state.username:"成员"),status:options.status||"",time:options.time?new Date(options.time):new Date()});if(remembered.inserted&&kind==="other"&&roomId!==state.currentRoomId)state.unread.set(roomId,(state.unread.get(roomId)||0)+1);if(roomId===state.currentRoomId)renderMessages();renderRooms();return remembered.message}
  function mergeServerMessage(item,fromHistory=false){const roomId=text(item.roomId),remembered=remember(roomId,{id:text(item.messageId),clientMessageId:text(item.clientMessageId),content:text(item.content),kind:item.senderId===state.username?"me":"other",sender:text(item.senderId),status:"sent",time:new Date(item.createdAt)});if(item.senderId===state.username){const pending=state.pending.get(text(item.clientMessageId));if(pending&&pending.roomId===roomId){clearTimeout(pending.timer);state.pending.delete(pending.clientMessageId)}}if(!fromHistory){trackUnread(roomId,item);if(roomId===state.currentRoomId)renderMessages();renderRooms()}return remembered.message}
  async function loadHistory(roomId){
    const version=state.sessionVersion,roomVersion=state.roomVersions.get(roomId)||0;
    const current=()=>version===state.sessionVersion&&roomVersion===(state.roomVersions.get(roomId)||0);
    // A second request after subscription catches messages committed during the first fetch.
    if(state.historyRequests.has(roomId))await state.historyRequests.get(roomId);
    if(!current()||!state.token)return;
    const request=(async()=>{
      state.loadingRooms.add(roomId);
      if(roomId===state.currentRoomId)renderMessages();
      try{
        const recovering=state.historyInitialized.has(roomId);
        let after=state.historyCursors.get(roomId)||"0";
        for(;;){
          const data=await api(`/api/v1/rooms/${encodeURIComponent(roomId)}/messages?after=${encodeURIComponent(after)}&limit=100`);
          if(!current())return;
          const items=Array.isArray(data.data?.messages)?data.data.messages:[];
          items.forEach(item=>{
            mergeServerMessage(item,true);
            if(recovering||roomId===state.currentRoomId)trackUnread(roomId,item);
          });
          if(items.length){
            const lastId=text(items[items.length-1].messageId);
            state.historyCursors.set(roomId,lastId);
            if(!recovering&&roomId!==state.currentRoomId)markHistoryBaseline(roomId,lastId);
          }else if(!state.historyCursors.has(roomId))state.historyCursors.set(roomId,"0");
          const next=text(data.data?.nextAfter);
          if(!next||next===after){state.historyInitialized.add(roomId);break}
          after=next;
        }
      }catch(error){if(current())toast(errorText(error,"历史消息加载失败"),"error")}
      finally{if(current()){state.loadingRooms.delete(roomId);if(roomId===state.currentRoomId)renderMessages();renderRooms()}}
    })();
    state.historyRequests.set(roomId,request);
    await request;
    if(state.historyRequests.get(roomId)===request)state.historyRequests.delete(roomId);
  }
  function findMessage(roomId,messageId){return(state.messages.get(roomId)||[]).find(message=>message.id===messageId)}
  function updateMessageStatus(roomId,messageId,status){const message=findMessage(roomId,messageId);if(!message)return;message.status=status;if(roomId===state.currentRoomId)renderMessages()}
  function updateLatestButton(){const distance=messageList.scrollHeight-messageList.scrollTop-messageList.clientHeight;$("latest-message-button").hidden=!state.currentRoomId||!(state.messages.get(state.currentRoomId)||[]).length||distance<64}
  function renderMessages(){
    const changedRoom=state.renderedRoomId!==state.currentRoomId;
    const follow=changedRoom||messageList.scrollHeight-messageList.scrollTop-messageList.clientHeight<64;
    const previousTop=messageList.scrollTop;
    state.renderedRoomId=state.currentRoomId;
    messageList.replaceChildren();
    $("latest-message-button").hidden=true;
    const loading=state.loadingRooms.has(state.currentRoomId);
    messageList.setAttribute("aria-busy",String(loading));
    if(loading&&!(state.messages.get(state.currentRoomId)||[]).length){renderSkeletons(messageList,"message-skeleton",3);return}
    const room=state.rooms.find(item=>item.id===state.currentRoomId);
    if(!room){const welcome=document.createElement("div");welcome.className="room-welcome";setText(welcome,"登录后创建或选择一个房间");messageList.appendChild(welcome);return}
    const messages=state.messages.get(room.id)||[];
    if(!messages.length){const welcome=document.createElement("div"),mark=document.createElement("div"),title=document.createElement("strong"),hint=document.createElement("span");welcome.className="room-welcome";mark.className="room-welcome-mark";setText(mark,initials(room.name));setText(title,room.name);setText(hint,"暂无消息");welcome.append(mark,title,hint);messageList.appendChild(welcome);return}
    let previousDate="";
    messages.forEach(message=>{const day=dateKey(message.time);if(day!==previousDate){const divider=document.createElement("div"),label=document.createElement("time");divider.className="date-divider";label.dateTime=message.time.toISOString();setText(label,displayDate(message.time));divider.appendChild(label);messageList.appendChild(divider);previousDate=day}if(message.kind==="system"){const node=document.createElement("div");node.className="system-message";setText(node,message.content);messageList.appendChild(node);return}const mine=message.kind==="me",row=document.createElement("div"),avatar=document.createElement("div"),stack=document.createElement("div"),meta=document.createElement("div"),sender=document.createElement("span"),time=document.createElement("time"),bubble=document.createElement("div");row.className=`message-row ${mine?"me":"other"}${message.status?` ${message.status}`:""}`;avatar.className="avatar message-avatar";avatar.dataset.tone=tone(message.sender);stack.className="message-stack";meta.className="message-meta";bubble.className="message-bubble";setText(avatar,initials(message.sender));setText(sender,message.sender);sender.title=message.sender;time.dateTime=message.time.toISOString();setText(time,message.time.toLocaleTimeString([],{hour:"2-digit",minute:"2-digit"}));setText(bubble,message.content);meta.append(sender,time);if(mine&&message.status){const status=document.createElement("span");status.className=`message-state ${message.status}`;setText(status,{sending:"发送中",sent:"已发出",failed:"发送失败"}[message.status]||"");meta.appendChild(status);if(message.status==="failed"){const retry=document.createElement("button");retry.className="message-retry";retry.type="button";setText(retry,"重试");retry.addEventListener("click",()=>retryMessage(room.id,message.id));meta.appendChild(retry)}}stack.append(meta,bubble);row.append(avatar,stack);messageList.appendChild(row)});
    messageList.scrollTop=follow?messageList.scrollHeight:previousTop;
    updateLatestButton();
  }
  function updateConnection(mode){$("status-dot").className=`status-dot ${mode}`;const label={online:"已连接",connecting:"连接中",offline:"已离线"}[mode]||"未连接";setText($("status-text"),label);$("status-dot").parentElement.title=label;$("status-dot").parentElement.setAttribute("aria-label",label);$("retry-button").hidden=mode!=="offline";setText($("user-state"),{online:"在线",connecting:"连接中",offline:"离线"}[mode]||"未连接");document.querySelector(".user-presence").classList.toggle("online",mode==="online");updateComposer()}
  function updateComposer(){const online=state.ws&&state.ws.readyState===WebSocket.OPEN&&state.subscribedRooms.has(state.currentRoomId),hasRoom=Boolean(state.currentRoomId),length=messageBytes(messageInput.value);sendButton.disabled=!online||!hasRoom||!messageInput.value.trim()||length>MAX_MESSAGE_LENGTH;messageInput.disabled=!hasRoom;messageInput.placeholder=!hasRoom?"选择房间后输入消息":!online?"连接恢复后即可发送消息":"输入消息…";setText(charCount,`${length} / ${MAX_MESSAGE_LENGTH} B`);charCount.classList.toggle("warning",length>MAX_MESSAGE_LENGTH*.9&&length<=MAX_MESSAGE_LENGTH);charCount.classList.toggle("invalid",length>MAX_MESSAGE_LENGTH)}
  function connectWebSocket(){
    if(!state.token||(state.ws&&[WebSocket.OPEN,WebSocket.CONNECTING].includes(state.ws.readyState)))return;
    clearTimeout(state.reconnectTimer);updateConnection("connecting");
    const protocol=location.protocol==="https:"?"wss":"ws",socket=new WebSocket(`${protocol}://${location.host}/ws`,["httpserver.v1",`bearer.${state.token}`]);
    state.ws=socket;state.subscriptions.clear();state.subscribedRooms.clear();state.subscriptionQueue=[];
    socket.addEventListener("open",()=>{if(state.ws!==socket)return;updateConnection("online");const rooms=new Set([state.currentRoomId,...[...state.pending.values()].map(item=>item.roomId),...state.rooms.map(room=>room.id)]);for(const roomId of rooms)subscribeRoom(roomId)});
    socket.addEventListener("message",event=>{
      if(state.ws!==socket)return;
      let data;try{data=JSON.parse(event.data)}catch(_){}
      if(data?.version===1&&data.type==="room.subscribed"){
        const roomId=state.subscriptions.get(data.requestId);
        if(roomId&&roomId===text(data.payload?.roomId)){
          state.subscriptions.delete(data.requestId);
          state.subscribedRooms.add(roomId);updateComposer();flushSubscriptions();
          state.reconnectDelay=1000;
          loadHistory(roomId).then(()=>{if(state.ws===socket)resumePending(roomId)});
        }
        return;
      }
      if(data?.type==="error"&&state.subscriptions.has(data.requestId)){
        const roomId=state.subscriptions.get(data.requestId);
        state.subscriptions.delete(data.requestId);
        if(data.error?.code==="dependency_unavailable"){
          socket.close(4001,"subscription retry");
        }else{
          for(const pending of state.pending.values())if(pending.roomId===roomId){clearTimeout(pending.timer);state.pending.delete(pending.clientMessageId);updateMessageStatus(roomId,pending.messageId,"failed")}
          toast("房间订阅失败，请检查房间成员权限","error");flushSubscriptions();
        }
        return;
      }
      handleSocket(event.data);
    });
    socket.addEventListener("error",()=>{if(state.ws===socket)updateConnection("connecting")});
    socket.addEventListener("close",event=>{
      if(state.ws!==socket)return;
      state.ws=null;state.subscriptions.clear();state.subscribedRooms.clear();state.subscriptionQueue=[];
      for(const pending of state.pending.values()){clearTimeout(pending.timer);updateMessageStatus(pending.roomId,pending.messageId,"sending")}
      updateConnection("offline");
      if(state.shouldReconnect&&state.token){clearTimeout(state.reconnectTimer);state.reconnectTimer=setTimeout(connectWebSocket,state.reconnectDelay);state.reconnectDelay=Math.min(state.reconnectDelay*2,30000)}
      if(event.code===1008||event.code===1003)toast("实时连接被服务器拒绝，请重新登录","error");
    });
  }
  function subscribeRoom(roomId){
    if(!roomId||!state.ws||state.ws.readyState!==WebSocket.OPEN||state.subscribedRooms.has(roomId)||[...state.subscriptions.values()].includes(roomId))return;
    const queued=state.subscriptionQueue.indexOf(roomId);
    if(queued>=0){if(roomId!==state.currentRoomId)return;state.subscriptionQueue.splice(queued,1)}
    if(roomId===state.currentRoomId)state.subscriptionQueue.unshift(roomId);else state.subscriptionQueue.push(roomId);flushSubscriptions();
  }
  function flushSubscriptions(){
    if(state.subscriptions.size||!state.subscriptionQueue.length||!state.ws||state.ws.readyState!==WebSocket.OPEN)return;
    const roomId=state.subscriptionQueue.shift(),requestId=identifier();
    state.subscriptions.set(requestId,text(roomId));
    try{state.ws.send(JSON.stringify({version:1,type:"room.subscribe",requestId,payload:{roomId:text(roomId)}}))}
    catch(_){state.subscriptions.delete(requestId);state.ws.close()}
  }
  function handleSocket(raw){let data;try{data=JSON.parse(raw)}catch(_){addMessage(state.currentRoomId,raw,"system","系统");return}if(data.version!==1)return;if(data.type==="message.created"&&data.payload?.message){mergeServerMessage(data.payload.message);return}if(data.type==="message.ack"&&data.payload){const clientMessageId=text(data.payload.clientMessageId),pending=state.pending.get(clientMessageId);if(!pending)return;clearTimeout(pending.timer);state.pending.delete(clientMessageId);const message=findMessage(pending.roomId,pending.messageId);if(message){message.id=text(data.payload.messageId);message.status="sent";message.time=new Date(data.payload.createdAt);sortMessageList(state.messages.get(pending.roomId)||[]);renderMessages();renderRooms()}return}if(data.type==="error"){const code=data.error?.code||"unknown",pending=[...state.pending.values()].find(item=>item.requestId===data.requestId);if(pending){clearTimeout(pending.timer);if(code==="dependency_unavailable")scheduleRetry(pending);else{state.pending.delete(pending.clientMessageId);updateMessageStatus(pending.roomId,pending.messageId,"failed")}}if(code!=="dependency_unavailable"){const messages={room_not_found:"房间不存在或已被关闭",room_membership_required:"请先加入当前房间",invalid_content:"消息内容无效"};addMessage(state.currentRoomId,messages[code]||data.error?.message||"服务器拒绝了这条操作","system","系统")}}}
  function scheduleRetry(pending){clearTimeout(pending.timer);if(pending.attempt>=4){state.pending.delete(pending.clientMessageId);updateMessageStatus(pending.roomId,pending.messageId,"failed");toast("消息确认超时，请手动重试","error");return}const delay=Math.min(1000*2**pending.attempt,8000);pending.timer=setTimeout(()=>dispatchMessage(pending.roomId,pending.messageId),delay)}
  function dispatchMessage(roomId,messageId){const message=findMessage(roomId,messageId);if(!message)return;const clientMessageId=message.clientMessageId||message.id;let pending=state.pending.get(clientMessageId);if(!pending){pending={clientMessageId,roomId,messageId,attempt:0,requestId:"",timer:null};state.pending.set(clientMessageId,pending)}const socket=state.ws;if(!socket||socket.readyState!==WebSocket.OPEN||!state.subscribedRooms.has(roomId)){updateMessageStatus(roomId,messageId,"sending");return}pending.attempt+=1;pending.requestId=identifier();updateMessageStatus(roomId,messageId,"sending");try{socket.send(JSON.stringify({version:1,type:"message.send",requestId:pending.requestId,payload:{roomId:text(roomId),clientMessageId,content:message.content}}));pending.timer=setTimeout(()=>scheduleRetry(pending),5000)}catch(_){scheduleRetry(pending)}}
  function resumePending(roomId){for(const pending of state.pending.values())if(pending.roomId===roomId)dispatchMessage(pending.roomId,pending.messageId)}
  function retryMessage(roomId,messageId){dispatchMessage(roomId,messageId)}
  function sendMessage(event){event.preventDefault();const content=messageInput.value.trim();if(state.composing||!content||messageBytes(content)>MAX_MESSAGE_LENGTH||!state.currentRoomId||!state.ws||state.ws.readyState!==WebSocket.OPEN||!state.subscribedRooms.has(state.currentRoomId))return;const clientMessageId=identifier(),message=addMessage(state.currentRoomId,content,"me",state.username,{id:clientMessageId,clientMessageId,status:"sending"});state.drafts.delete(state.currentRoomId);renderRooms();messageInput.value="";messageList.scrollTop=messageList.scrollHeight;resizeInput();updateComposer();messageInput.focus();if(message)dispatchMessage(state.currentRoomId,message.id)}
  async function createRoom(event){event.preventDefault();const name=roomNameInput.value.trim();if(!name||new TextEncoder().encode(name).length>128){formMessage(roomMessage,"房间名称需为 1-128 字节");return}$("room-submit").disabled=true;formMessage(roomMessage,"");try{const data=await api("/api/v1/rooms",{method:"POST",body:JSON.stringify({name})}),roomId=text(data.data?.room?.id);await fetchRooms();if(roomId)await switchRoom(roomId);roomDialog.close();roomNameInput.value="";toast("房间创建成功","success")}catch(error){formMessage(roomMessage,errorText(error,"房间创建失败"))}finally{$("room-submit").disabled=false}}
  async function joinRoomById(event){event.preventDefault();const roomId=joinRoomIdInput.value.trim();if(!roomId||new TextEncoder().encode(roomId).length>128){formMessage(joinRoomMessage,"请输入有效的房间 ID");return}$("join-room-submit").disabled=true;formMessage(joinRoomMessage,"");try{await api(`/api/v1/rooms/${encodeURIComponent(roomId)}/members`,{method:"POST"});await fetchRooms();await switchRoom(roomId);joinRoomDialog.close();joinRoomIdInput.value="";toast("已加入房间","success")}catch(error){formMessage(joinRoomMessage,errorText(error,"加入房间失败"))}finally{$("join-room-submit").disabled=false}}
  async function leaveCurrentRoom(){const roomId=state.currentRoomId;if(!roomId)return;try{await api(`/api/v1/rooms/${encodeURIComponent(roomId)}/members/me`,{method:"DELETE"});state.roomVersions.set(roomId,(state.roomVersions.get(roomId)||0)+1);state.messages.delete(roomId);state.subscribedRooms.delete(roomId);state.subscriptionQueue=state.subscriptionQueue.filter(id=>id!==roomId);state.unread.delete(roomId);state.unreadIds.delete(roomId);state.readThrough.delete(roomId);state.historyCursors.delete(roomId);state.historyInitialized.delete(roomId);state.historyRequests.delete(roomId);state.loadingRooms.delete(roomId);state.drafts.delete(roomId);for(const pending of state.pending.values())if(pending.roomId===roomId){clearTimeout(pending.timer);state.pending.delete(pending.clientMessageId)}if(state.currentRoomId===roomId){state.currentRoomId="";messageInput.value="";resizeInput();localStorage.removeItem(keys.room)}await fetchRooms();subscribeRoom(state.currentRoomId);toast("已退出房间","success")}catch(error){if(error.code!=="stale_session")toast(error.code==="room_owner_cannot_leave"?"房主暂时不能退出房间":errorText(error,"退出房间失败"),"error")}}
  function resizeInput(){messageInput.style.height="auto";messageInput.style.height=`${Math.min(messageInput.scrollHeight,132)}px`}
  function toggleMobile(open){$("sidebar").classList.toggle("open",open);$("mobile-scrim").hidden=!open;$("mobile-menu").setAttribute("aria-expanded",String(open))}
  async function logout(){state.shouldReconnect=false;clearTimeout(state.reconnectTimer);if(state.ws)state.ws.close(1000,"logout");state.ws=null;for(const pending of state.pending.values())clearTimeout(pending.timer);try{if(state.token)await api("/logout",{method:"POST"})}catch(_){}finally{clearSession();state.rooms=[];state.messages.clear();state.unread.clear();state.pending.clear();updatePageTitle();usernameInput.value="";passwordInput.value="";setAuthMode("login");showAuth("你已退出登录",true)}}
  async function resume(){if(!state.token||!state.username){showAuth();return}await enterChat()}
  $("login-tab").addEventListener("click",()=>setAuthMode("login"));$("register-tab").addEventListener("click",()=>setAuthMode("register"));authForm.addEventListener("submit",submitAuth);$("room-search").addEventListener("input",renderRooms);$("create-room-button").addEventListener("click",()=>{formMessage(roomMessage,"");roomNameInput.value="";roomDialog.showModal();roomNameInput.focus()});$("join-room-button").addEventListener("click",()=>{formMessage(joinRoomMessage,"");joinRoomIdInput.value="";joinRoomDialog.showModal();joinRoomIdInput.focus()});$("close-room-dialog").addEventListener("click",()=>roomDialog.close());$("cancel-room-button").addEventListener("click",()=>roomDialog.close());$("close-join-room-dialog").addEventListener("click",()=>joinRoomDialog.close());$("cancel-join-room-button").addEventListener("click",()=>joinRoomDialog.close());roomForm.addEventListener("submit",createRoom);joinRoomForm.addEventListener("submit",joinRoomById);$("leave-room-button").addEventListener("click",leaveCurrentRoom);$("logout-button").addEventListener("click",logout);$("retry-button").addEventListener("click",connectWebSocket);$("mobile-menu").addEventListener("click",()=>toggleMobile(true));$("mobile-scrim").addEventListener("click",()=>toggleMobile(false));$("composer").addEventListener("submit",sendMessage);
  for(const mode of ["all","unread"]){
    const tab=$(mode+"-rooms-filter");
    tab.addEventListener("click",()=>{state.roomFilter=mode;renderRooms()});
    tab.addEventListener("keydown",event=>{
      if(!["ArrowLeft","ArrowRight","Home","End"].includes(event.key))return;
      event.preventDefault();state.roomFilter=event.key==="Home"?"all":event.key==="End"?"unread":mode==="all"?"unread":"all";
      renderRooms();$(state.roomFilter+"-rooms-filter").focus();
    });
  }
  $("password-toggle").addEventListener("click",()=>setPasswordVisible(passwordInput.type==="password"));
  $("copy-room-button").addEventListener("click",async()=>{if(!state.currentRoomId)return;try{await navigator.clipboard.writeText(state.currentRoomId);toast("房间 ID 已复制","success")}catch(_){toast("无法访问剪贴板，请复制房间标题下的 ID","error")}});
  $("close-sidebar-button").addEventListener("click",()=>{toggleMobile(false);$("mobile-menu").focus()});
  $("latest-message-button").addEventListener("click",()=>{messageList.scrollTop=messageList.scrollHeight;updateLatestButton()});
  messageList.addEventListener("scroll",updateLatestButton,{passive:true});
  window.addEventListener("resize",updateLatestButton);
  document.addEventListener("keydown",event=>{if(event.key==="Escape"&&!roomDialog.open&&!joinRoomDialog.open&&$("sidebar").classList.contains("open")){toggleMobile(false);$("mobile-menu").focus()}});
  messageInput.addEventListener("compositionstart",()=>{state.composing=true});
  messageInput.addEventListener("compositionend",()=>{state.composing=false});
  messageInput.addEventListener("input",()=>{if(state.currentRoomId)state.drafts.set(state.currentRoomId,messageInput.value);resizeInput();updateComposer();renderRooms()});
  messageInput.addEventListener("keydown",event=>{if(event.isComposing||state.composing||event.keyCode===229)return;if(event.key==="Enter"&&!event.shiftKey){event.preventDefault();$("composer").requestSubmit()}});
  window.addEventListener("beforeunload",()=>{state.shouldReconnect=false;clearTimeout(state.reconnectTimer)});setAuthMode("login");updateComposer();resume();
})();
