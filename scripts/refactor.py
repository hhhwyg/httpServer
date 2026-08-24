import sys

with open("/home/wyg/WebServer/WebServer/httpdata/HttpData.cpp", "r") as f:
    lines = f.readlines()

new_lines = []
skip = False

for i, line in enumerate(lines):
    # 1. Skip MimeType vars
    if line.startswith("pthread_once_t MimeType::once_control = PTHREAD_ONCE_INIT;"):
        skip = True
    elif line.startswith("const __uint32_t DEFAULT_EVENT"):
        if skip:
            skip = False
    
    # 2. Skip handleRegister to checkLogin
    if line.startswith(" void HttpData::handleRegister() {") or line.startswith("void HttpData::handleRegister() {"):
        skip = True
    elif line.startswith(" ProcessState HttpData::getRequestStatus()") or line.startswith("ProcessState HttpData::getRequestStatus()"):
        if skip:
            skip = False

    # 3. Skip computeAcceptKey and MimeType init/getMime
    if line.startswith("// 辅助函数：计算 WebSocket 握手 Key"):
        skip = True
    elif line.startswith("HttpData::HttpData(EventLoop *loop, int connfd)"):
        if skip:
            skip = False

    # 4. Skip extractUsername and base64UrlDecode
    if line.startswith("std::string HttpData::extractUsername(const std::string& token) {"):
        skip = True
    elif line.startswith("AnalysisState HttpData::analysisRequest() {"):
        if skip:
            skip = False

    if not skip:
        # Check for analysisRequest logic to replace
        if "handleRegister();" in line and "UserController::handleRegister" not in line:
            new_lines.append("        UserController::handleRegister(shared_from_this());\n")
            continue
        if "handleLogin();" in line and "UserController::handleLogin" not in line:
            new_lines.append("        UserController::handleLogin(shared_from_this());\n")
            continue
        if "handleCreateRoom();" in line and "RoomController::handleCreateRoom" not in line:
            new_lines.append("        RoomController::handleCreateRoom(shared_from_this());\n")
            continue
        if "handleGetRoomList();" in line and "RoomController::handleGetRoomList" not in line:
            new_lines.append("        RoomController::handleGetRoomList(shared_from_this());\n")
            continue
        
        # Check for WebSocketHandshake logic
        if "!verifyJWT(token)" in line:
            new_lines.append('    if (token.empty() || !CryptoUtil::verifyJWT(token)) {\n')
            continue
        if "std::string username_ = extractUsername(token);" in line:
            new_lines.append('    std::string username_ = CryptoUtil::extractUsername(token);\n')
            continue
        if "std::string acceptKey = computeAcceptKey(clientKey);" in line:
            new_lines.append('    std::string acceptKey = CryptoUtil::computeAcceptKey(clientKey);\n')
            continue

        if line.startswith("#include\"ChatManager.h\"") or line.startswith("#include \"ChatManager.h\""):
            new_lines.append(line)
            new_lines.append("#include \"UserController.h\"\n")
            new_lines.append("#include \"RoomController.h\"\n")
            new_lines.append("#include \"CryptoUtil.h\"\n")
            new_lines.append("#include \"MimeType.h\"\n")
            continue
            
        new_lines.append(line)

with open("/home/wyg/WebServer/WebServer/httpdata/HttpData.cpp", "w") as f:
    f.writelines(new_lines)
