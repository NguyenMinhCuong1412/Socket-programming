# Giải thích chi tiết project SOCKET — Hybrid FTP Client/Server (C++ / Winsock)
### (Tài liệu hoàn chỉnh — phản ánh đúng bản `Socket_fixed.zip` mới nhất: RDT Go-Back-N + AIMD, cổng UDP tự động thương lượng)

---

## 0. Bức tranh tổng thể

Đây là một **FTP client/server tự viết**, mô phỏng đúng kiến trúc FTP thật (RFC 959):

- **Kênh điều khiển (Control Channel) — TCP, cổng 8080**: nơi Client gửi lệnh dạng text (`USER`, `PASS`, `STOR`, `RETR`, ...) và Server trả về mã phản hồi kiểu FTP (`220`, `226`, `550`, ...). Kênh này luôn mở suốt phiên làm việc. Đây là **cổng "well-known" duy nhất** cần cố định — vì đây là cổng Client bắt buộc phải biết trước để mở kết nối ban đầu; không có kênh nào khác để báo trước cổng này.
- **Kênh dữ liệu (Data Channel) — UDP**: nơi truyền nội dung file thật sự. Vì UDP không đảm bảo gói tin đến nơi/đến đủ/đến đúng thứ tự, project tự cài một giao thức tin cậy gọi là **RDT (Reliable Data Transfer)** theo mô hình **Go-Back-N (sliding window) + Congestion Control kiểu AIMD** — đúng kiến thức lõi của tầng Transport trong môn Mạng máy tính.

Mỗi Client kết nối vào Server sẽ được Server tạo một **thread riêng** để xử lý (mô hình thread-per-connection), và trong thread đó, khi có lệnh truyền file (STOR/RETR/...), Server lại tách ra **một thread phụ khác** chỉ để chạy việc truyền dữ liệu UDP — nhờ vậy luồng chính vẫn rảnh để nhận lệnh mới (kể cả lệnh `ABOR` để hủy transfer đang chạy).

Cấu trúc thư mục:
```
SERVER - 7/SERVER/   → chương trình Server
CLIENT - 7/CLIENT/   → chương trình Client
```

`RdtPacket.h`, `RdtPacket.cpp` và `DataChannel.cpp` ở **Client và Server giống hệt nhau (byte-by-byte)**. Điều này hợp lý vì giao thức RDT là đối xứng — cả hai bên đều cần gửi được và nhận được theo cùng một luật.

---

## 1. Luồng hoạt động end-to-end

### 1.1. Bắt tay kết nối
1. Server (`Server.cpp`) khởi động Winsock, tạo thư mục `server_root/` (gốc lưu file của mọi client), mở `ControlChannel` lắng nghe TCP cổng 8080.
2. Client (`Client.cpp`) khởi động Winsock, `connect()` tới `127.0.0.1:8080`.
3. Server `accept()` được kết nối → tạo `std::thread` riêng chạy `ControlChannel::handleClient()`, gửi `"220 Service ready"`.
4. Trong thread đó, Server tạo một `Session` (trạng thái riêng của client này: đã đăng nhập chưa, đang ở thư mục nào, ACTIVE hay PASSIVE...) và một `CommandHandler` (bộ xử lý lệnh).

### 1.2. Vòng lặp lệnh
- **Client**: thread chính đọc bàn phím (`ftp> ...`), gửi câu lệnh qua TCP; một **thread nền riêng** (`receiverThread`) liên tục `recv()` phản hồi từ Server. Tách hai thread này ra là để bạn vẫn gõ được `ABOR` trong lúc một lệnh khác (như RETR) đang chờ dữ liệu — nếu dùng chung 1 thread, `recv()` chờ dữ liệu sẽ chặn luôn bàn phím.
- **Server**: mỗi lần `recv()` được 1 dòng lệnh, `parseCmd()` tách ra `command` + `argument`, rồi `CommandHandler::handle()` định tuyến (dispatch) tới đúng hàm xử lý (`handleUSER`, `handleSTOR`, ...) dựa trên `enum class FtpCommand`.

### 1.3. Khi có lệnh truyền file (STOR/RETR/STOU/APPE) — TCP điều phối, UDP truyền dữ liệu thật
1. Server nhận lệnh, mở một `DataChannel` (UDP), **bind với `port=0` để hệ điều hành tự cấp một cổng còn trống** (trừ trường hợp PASSIVE — xem mục 3).
2. Server hỏi lại cổng thật vừa được cấp bằng `DataChannel::getBoundPort()` (dùng `getsockname()`), rồi **nhúng cổng đó vào phản hồi `150`** dưới dạng `" PORT=<n>"` (hàm `appendPortIfNeeded()`), trừ khi đang PASSIVE (Client đã biết cổng từ trước qua mã `227` rồi).
3. Server **tách một thread phụ** (`transferThread`) để thực sự đọc/ghi file qua UDP+RDT — thread chính quay lại nhận lệnh mới ngay lập tức.
4. Client thấy phản hồi `"150 ..."` trong `receiverLoop()` → đọc ra cổng thật (`parseEmbeddedPort()`) nếu có, rồi tự động gọi `doDataTransfer()` để mở `DataChannel` phía mình và bắt đầu gửi/nhận qua UDP đúng cổng đó.
5. Khi xong, thread phụ của Server gửi tiếp `"226 Transfer complete"` (hoặc `"426 ..."` nếu lỗi/bị hủy) qua TCP.

### 1.4. ACTIVE vs PASSIVE (giống FTP thật)
- **ACTIVE**: Client tự chọn cổng UDP của mình (giờ là cổng **ngẫu nhiên do OS cấp**, không còn cố định), báo cho Server qua lệnh `PORT h1,h2,h3,h4,p1,p2` → khi truyền, **Server chủ động gửi tới** địa chỉ đó.
- **PASSIVE**: Client gửi `PASV` → Server tự chọn 1 cổng (dải 6000–6999), mở sẵn, trả về địa chỉ qua mã `227` → **Client chủ động kết nối tới** Server. Vì UDP không có handshake như TCP, khi Server cần *gửi* file cho Client ở chế độ PASSIVE (lệnh RETR), Server chưa biết chính xác địa chỉ nguồn của Client, nên phải chờ Client gửi một **gói "probe"** nhỏ trước (`sendProbe`) để "học" được địa chỉ (`sendFileAfterHandshake`) — kỹ thuật NAT traversal đơn giản kiểu UDP hole punching thu nhỏ.
- **Tự động hóa ACTIVE khi gõ RETR mà chưa PORT/PASV**: Client giờ có hàm `autoNegotiateActivePort()` — nếu bạn gõ `RETR` mà trước đó chưa từng gõ `PORT` hay `PASV`, Client **tự ngầm làm giúp** việc gửi lệnh `PORT` với một cổng UDP ngẫu nhiên do OS cấp, rồi mới gửi `RETR` thật. Nhờ vậy `handleRetr` phía Server gần như không bao giờ gặp trường hợp `DataMode::NONE` nữa (nếu gặp, Server trả lỗi `425` thay vì đoán bừa một cổng).

---

## 2. Cơ chế RDT — Go-Back-N (Sliding Window) + Congestion Control AIMD

Đây là "trái tim" của kênh dữ liệu, nơi biến UDP (không tin cậy) thành một kênh truyền tin cậy.

### 2.1. Cấu trúc gói tin (`RdtPacket.h`)
```
[seqNum 4B][flags 1B][checksum 2B][payloadLength 2B][payload...]
```
- **flags**: `FLAG_DATA` (mang dữ liệu), `FLAG_ACK` (xác nhận), `FLAG_FIN` (báo kết thúc truyền).
- **seqNum**: **tăng dần liên tục 0, 1, 2, 3...** (khác Stop-and-Wait cũ chỉ toggle 0/1) — mỗi chunk 1024B có 1 số thứ tự riêng trong toàn phiên truyền.
  - Trong gói **DATA**: là số thứ tự của chunk đó.
  - Trong gói **ACK**: mang tính **cumulative** — nghĩa là "đã nhận liên tục, không thiếu, tới hết seq này". Giá trị đặc biệt `0xFFFFFFFF` = "chưa nhận được gói hợp lệ nào".
  - Trong gói **FIN**: mang tổng số gói DATA đã gửi — để bên nhận tự xác minh đã nhận đủ chưa trước khi kết thúc.
- **checksum**: kiểu Internet checksum (1's complement sum, giống checksum trong header IP/TCP/UDP thật) để phát hiện lỗi bit — cộng từng cặp 2 byte, pad 0 nếu lẻ byte, "fold carry" (cộng phần tràn 16-bit cao vào 16-bit thấp), rồi đảo bit.
- Hằng số cấu hình:
  - `RDT_TIMEOUT_MS = 500`: thời gian chờ ACK thật (đo bằng đồng hồ) cho gói cũ nhất chưa được ACK.
  - `RDT_POLL_MS = 50`: chu kỳ poll `recvfrom()` — ngắn hơn nhiều so với timeout thật, để vừa có thể gửi thêm gói mới vào cửa sổ, vừa kiểm tra timeout kịp thời.
  - `RDT_MAX_RETRIES = 20`: số vòng Go-Back-N tối đa trước khi báo lỗi hẳn.
  - `RDT_INITIAL_WINDOW = 4`, `RDT_MIN_WINDOW = 1`, `RDT_MAX_WINDOW = 32`: cửa sổ khởi đầu / tối thiểu (tương đương Stop-and-Wait khi mạng quá tệ) / tối đa (chặn để không làm ngập mạng).

`serializePacket()`/`deserializePacket()` đóng gói/mở gói giữ nguyên logic cũ (ghi từng field theo network byte order, tính checksum sau khi ghi hết field khác, kiểm tra `payloadLength` khớp số byte thực nhận trước khi tin gói).

### 2.2. Hàm gửi `rdtSend()` — Go-Back-N với cửa sổ trượt

**Bước chuẩn bị**: toàn bộ file được **chia sẵn thành từng gói DATA và serialize một lần** (`vector<vector<char>> serializedPkts`), lưu lại để gửi lại (retransmit) nhiều lần mà không cần build lại gói.

**3 biến trạng thái chính của cửa sổ:**
- `base`: số thứ tự gói **cũ nhất chưa được ACK** (biên trái cửa sổ).
- `nextSeq`: số thứ tự gói **kế tiếp chưa từng gửi** (biên phải cửa sổ).
- `window`: kích thước cửa sổ hiện tại, tự điều chỉnh theo AIMD.

Chỉ có **một timer duy nhất**, canh cho gói `base` — dù có 4 tới 32 gói đang "bay" cùng lúc, hệ thống chỉ cần theo dõi đúng 1 mốc thời gian, đúng cách cài Go-Back-N chuẩn theo sách Kurose & Ross.

**Vòng lặp chính** (`while (base < totalChunks)`) mỗi vòng làm:
1. **Gửi thêm gói mới** miễn còn trong giới hạn cửa sổ: `while (nextSeq < totalChunks && nextSeq < base + window)`.
2. **Khởi động lại timer** nếu còn gói chưa ACK và chưa có timer nào chạy.
3. **Poll `recvfrom()` trong `RDT_POLL_MS`** (50ms) chờ ACK.
4. **Xử lý ACK nhận được**: nếu là cumulative ACK hợp lệ và vượt qua `base` hiện tại → trượt `base` tới, `window = min(window+1, RDT_MAX_WINDOW)` (**Additive Increase**), reset bộ đếm lỗi, tắt timer (sẽ tự bật lại ở vòng sau nếu cần).
5. **Kiểm tra timeout thật** (dùng `std::chrono::steady_clock`, độc lập với thời gian poll socket): nếu đã quá 500ms kể từ lúc timer khởi động → `window = max(window/2, RDT_MIN_WINDOW)` (**Multiplicative Decrease**) → **gửi lại toàn bộ cửa sổ hiện có** `[base, nextSeq)` — đây là đặc trưng Go-Back-N (khác Selective Repeat chỉ gửi lại đúng gói bị mất).

Sau khi toàn bộ DATA đã được ACK hết, gửi gói **FIN theo kiểu Stop-and-Wait đơn giản** (chỉ 1 gói duy nhất, không cần cửa sổ nữa vì đã hết dữ liệu để gửi).

### 2.3. Hàm nhận `rdtReceive()` — nhận nghiêm ngặt theo thứ tự

- Gói đến **đúng thứ tự** (`seqNum == expectedSeq`) → deliver (append vào `outData`), tăng `expectedSeq`.
- Gói **trùng lặp** (`seqNum < expectedSeq`, do ACK cũ bị mất nên bên gửi gửi lại) → không deliver lại, chỉ re-ACK.
- Gói **đến sớm** (`seqNum > expectedSeq`) → **loại bỏ hoàn toàn, không đệm lại** — đây chính là điểm phân biệt Go-Back-N với Selective Repeat (Selective Repeat sẽ đệm lại chờ ráp, Go-Back-N thì "vứt" luôn và chấp nhận bên gửi phải gửi lại cả cửa sổ khi timeout).
- Sau mọi trường hợp, luôn trả lời bằng **cumulative ACK = expectedSeq - 1** (hoặc `0xFFFFFFFF` nếu chưa nhận được gì hợp lệ) — nhờ đó bên gửi luôn biết chính xác cần Go-Back-N từ đâu.
- Khi nhận `FIN`: nếu `seqNum` của FIN khớp đúng `expectedSeq` hiện tại (đã nhận đủ, không thiếu gói nào) → ACK và kết thúc; nếu chưa khớp (còn thiếu gói giữa chừng, coi như FIN đến sớm/bất thường) → bỏ qua, tiếp tục chờ nhận nốt DATA còn thiếu.
- Checksum sai ở bất kỳ gói nào → **DROP âm thầm, không ACK** — buộc bên gửi phải tự retransmit khi timeout.

### 2.4. `sendFile`/`receiveFile`/`sendFileAfterHandshake`/`sendProbe`
Các hàm bọc quanh `rdtSend`/`rdtReceive` để đọc/ghi vào file thật (`ifstream`/`ofstream` chế độ `ios::binary`):
- `receiveFile(filepath, append)`: mở file (`ios::trunc` hoặc `ios::app`), gọi `rdtReceive()`, ghi toàn bộ dữ liệu nhận được vào file.
- `sendFile(filepath, destIp, destPort)`: đọc toàn bộ file vào buffer, gọi `rdtSend()` gửi tới địa chỉ đích.
- `sendFileAfterHandshake(filepath)`: dùng khi Server RETR ở chế độ PASSIVE — chờ nhận 1 gói "probe" qua RDT để học địa chỉ IP:port thật của Client, rồi mới `sendFile()` tới đúng địa chỉ đó.
- `sendProbe(destIp, destPort)`: dùng ở Client khi RETR ở chế độ PASSIVE — gửi 1 byte `'R'` qua RDT để Server học được địa chỉ của mình.
- `getBoundPort()`: hàm mới, dùng `getsockname()` lấy cổng UDP thật đã được OS cấp khi bind với `port=0` — cần thiết để Server biết cổng nào cần báo cho Client qua `" PORT=<n>"`.
- `stop()`: dùng `atomic<SOCKET>::exchange()` để đảm bảo chỉ đúng 1 thread thực sự đóng socket dù nhiều thread cùng gọi (ví dụ cả luồng transfer tự kết thúc lẫn `ABOR` cùng gọi).

---

## 3. Cổng UDP dữ liệu: OS tự cấp thay vì cố định

### 3.1. Vấn đề của cách làm cũ (đã bỏ)
Trước đây dùng 2 cổng UDP cố định: `SERVER_DATA_PORT=8081` (Server nghe khi nhận STOR/APPE/STOU) và `CLIENT_DATA_PORT=8082` (Client nghe khi RETR mặc định). Vấn đề: chạy **nhiều Client trên cùng 1 máy** sẽ tranh nhau đúng 1 cổng 8082 → xung đột (`bind failed`); cổng cố định cũng dễ bị tiến trình khác chiếm.

### 3.2. Cách làm hiện tại
`lib.h` (cả 2 bên) chỉ còn giữ `CONTROL_PORT = 8080`. Cổng dữ liệu UDP giờ luôn **để hệ điều hành tự cấp** (`bind` với `port=0`) rồi báo cho bên kia biết qua kênh điều khiển TCP:

- **Phía Server** (`CmdHandler.cpp`):
  - `pickListenPort()`: PASSIVE vẫn dùng cổng đã hẹn trước qua `PASV` (bắt buộc cố định vì đã báo qua mã `227`); ACTIVE/NONE trả về `0` để OS tự chọn.
  - `appendPortIfNeeded(session, boundPort, baseMsg)`: nhúng `" PORT=<n>"` vào cuối phản hồi `"150 ..."` (trước `\r\n`) khi không phải PASSIVE. `handleStor`/`handleStou`/`handleAppe` đều gọi hàm này.
  - `handleRetr`: nếu `mode == DataMode::NONE` → trả lỗi `"425 ... send PORT or PASV before RETR"` thay vì âm thầm dùng cổng cố định như trước.

- **Phía Client** (`ControlChannel.h`/`.cpp`):
  - `parseEmbeddedPort(reply, outPort)`: đọc `"PORT=<n>"` từ phản hồi `150` của Server.
  - `serverUploadPort`: lưu cổng đó, dùng khi Client gửi dữ liệu cho `STOR`/`STOU`/`APPE`.
  - `autoNegotiateActivePort()`: khi gõ `RETR` mà chưa từng gõ `PORT`/`PASV`, Client tự động bind thử 1 socket UDP tạm với `port=0` để hỏi OS cổng còn trống, lấy IP cục bộ qua `getsockname()` trên chính socket điều khiển, rồi tự gửi ngầm lệnh `PORT h1,h2,h3,h4,p1,p2` tới Server và cập nhật `dataMode = ACTIVE` ngay lập tức (không cần đợi phản hồi `200`, vì thứ tự gửi trên TCP đảm bảo lệnh `RETR` gửi ngay sau đó chắc chắn tới Server *sau* lệnh `PORT` ngầm này).
  - Trong `run()`: gọi `autoNegotiateActivePort()` ngay trước khi gửi `RETR`, nếu `dataMode` đang là `NONE`.

### 3.3. Vì sao hợp lý
Đúng tinh thần FTP thật — RFC 959 chỉ yêu cầu 2 bên *biết được* cổng của nhau qua `PORT`/`PASV`, không bắt phải là số cố định. Cách này giải quyết đúng vấn đề đụng cổng khi chạy nhiều Client, mà vẫn giữ nguyên tư duy ACTIVE/PASSIVE ban đầu.

---

## 4. SERVER — giải thích từng file

### 4.1. `lib.h` — cấu hình & thư viện dùng chung
- Include toàn bộ thư viện cần: `<filesystem>` (thao tác thư mục/file kiểu hiện đại C++17), `<thread>`/`<mutex>`/`<atomic>` (đa luồng), `<winsock2.h>` (socket Windows).
- Chỉ còn 1 hằng số cổng cố định: `CONTROL_PORT = 8080`. `CHUNK_SIZE = 1024`.
- `SERVER_ROOT`: thư mục gốc vật lý trên đĩa, mọi đường dẫn "ảo" của Client (`/`, `/abc`) đều được ánh xạ (map) vào bên trong thư mục này — cơ chế **sandbox**, ngăn Client dùng `../../..` để đọc file ngoài phạm vi cho phép.
- `g_coutMutex`: mutex toàn cục tránh nhiều thread in đè lên nhau trên console.
- `enum class DataMode { NONE, ACTIVE, PASSIVE }` và `enum class FtpCommand {...}`: dùng enum thay vì so sánh chuỗi liên tục, nhanh và ít lỗi gõ nhầm hơn.

### 4.2. `Session.h` / `Session.cpp` — trạng thái riêng của mỗi Client
Mỗi kết nối Client có **một `Session` độc lập** (không dùng biến toàn cục), lưu:
- `isLoggedIn`, `userName`: trạng thái đăng nhập (chỉ mang tính hình thức, không kiểm tra mật khẩu thật).
- `currentDir`: thư mục "ảo" hiện tại.
- `dataType` (A/I), `transferMode` (S/B/C — chỉ hỗ trợ S).
- `renameFrom`: lưu tạm tên file khi gõ `RNFR` chờ `RNTO` hoàn tất.
- `dataMode`, `activeIp/activePort`, `passivePort`: trạng thái ACTIVE/PASSIVE.
- `activeDataChannel`: **con trỏ quan sát** (observer, không sở hữu) tới `DataChannel` đang chạy transfer, chỉ để lệnh `ABOR` có thể gọi `stop()` đóng socket từ một thread khác.

Điểm đáng chú ý nhất là phần comment giải thích **lỗi double-free đã từng gặp và cách sửa**: ban đầu `setActiveDataChannel(nullptr)` từng gọi `delete` con trỏ đó, nhưng vì đối tượng `DataChannel` thật sự được quản lý bởi một `shared_ptr` khác nằm trong lambda của `transferThread`, khi `shared_ptr` đó tự hủy ở cuối lambda, nó sẽ `delete` chính đối tượng đã bị `delete` trước đó → crash/heap corruption khó tái hiện. Cách sửa: hàm này **không bao giờ `delete`**, chỉ lưu/xóa địa chỉ con trỏ.

`abortActiveTransfer()`: được `mutex` bảo vệ, gọi `activeDataChannel->stop()` — `stop()` sẽ `closesocket()` khiến `recvfrom()`/`sendto()` đang bị block ở thread phụ lập tức trả về lỗi, nhờ đó thread phụ biết để thoát vòng lặp.

### 4.3. `ControlChannel.h` / `ControlChannel.cpp` — kênh điều khiển TCP (không đổi so với bản đầu)
- `start()`: tạo socket TCP, `bind()` vào `INADDR_ANY:8080`, `listen()`.
- `run()`: vòng lặp `accept()` **vô hạn** — mỗi Client mới được `accept()` trả về 1 socket riêng, rồi `thread(...).detach()` tạo thread mới xử lý và **tách rời**, để thread chính quay lại `accept()` Client tiếp theo ngay.
- `handleClient(clientSocket, clientIp)`: xử lý một phiên trọn vẹn — gửi `220`, khởi tạo `Session` + `CommandHandler`, vòng lặp `recv()` lệnh → `parseCmd()` → `handler.handle()` → `send()` phản hồi, cho tới khi Client ngắt kết nối hoặc gõ `QUIT`.
- `stop()`: đóng socket lắng nghe.

### 4.4. `CmdHandler.h` / `CmdHandler.cpp` — bộ não xử lý lệnh FTP (file lớn nhất)

**Hàm tiện ích:**
- `parseCmd(raw, cmd, arg)`: cắt bỏ `\r\n`, tách token đầu tiên làm `command`, phần còn lại làm `argument`, viết hoa `command`.
- `toFtpCommand(cmd)`: chuyển chuỗi lệnh sang `enum FtpCommand` để `switch-case` nhanh và rõ ràng hơn `if-else` chuỗi.
- `resolvePath(session, arg, outLogical)`: **hàm quan trọng nhất về bảo mật** — nhận đường dẫn Client gửi, chuẩn hóa bằng `lexically_normal()` (xử lý `..`, `.`, không cho vượt quá gốc), rồi map sang đường dẫn vật lý thật bên trong `SERVER_ROOT` bằng `weakly_canonical()` — chặn path traversal.
- `pickListenPort(session)`: PASSIVE thì trả về cổng đã hẹn trước qua `PASV`; ACTIVE/NONE thì trả về `0` để OS tự cấp cổng ngẫu nhiên.
- `appendPortIfNeeded(session, boundPort, baseMsg)`: nhúng `" PORT=<n>"` vào phản hồi `150` khi không PASSIVE, để Client biết cổng thật OS vừa cấp.
- `joinPreviousTransfer()`: trước khi bắt đầu 1 lệnh mới, luôn `join()` chờ `transferThread` cũ (nếu còn) hoàn tất — tránh 2 transfer chạy chồng lên nhau trên cùng 1 Session.

**Các hàm `handleXxx` — mỗi hàm ứng với đúng 1 lệnh FTP**, đa số theo khuôn: kiểm tra đăng nhập → kiểm tra tham số → xử lý → trả mã phản hồi kiểu `"2xx/4xx/5xx <message>\r\n"` (đúng chuẩn RFC 959):

| Hàm | Việc làm chính |
|---|---|
| `handleUser`/`handlePass` | Lưu username, đặt `isLoggedIn=true` (không kiểm tra mật khẩu thật) |
| `handlePwd` | Trả về `currentDir` |
| `handleType`/`handleMode` | Chỉ chấp nhận `TYPE {A,I}` và `MODE S` |
| `handleSize`/`handleMdtm`/`handleStat` | Truy vấn `fs::file_size`, `fs::last_write_time` |
| **`handleStor`** | Nhận file từ Client: mở `DataChannel` với `pickListenPort()`, `getBoundPort()` + `appendPortIfNeeded()` báo cổng thật, trả `150`, **tách thread phụ** gọi `dc->receiveFile()`, xong gửi `226`/`426` |
| **`handleRetr`** | Gửi file cho Client: nếu `mode==NONE` trả lỗi `425` ngay; PASSIVE dùng `sendFileAfterHandshake`, ACTIVE dùng `sendFile` tới đúng `activeIp:activePort` Client đã báo, cũng chạy trong thread phụ |
| `handleCwd`/`handleCdup` | Đổi `currentDir` sau khi xác nhận thư mục đích tồn tại |
| `handleMkd`/`handleRmd` | Tạo/xóa thư mục (RMD chỉ xóa được thư mục **rỗng**) |
| `handleList`/`handleNlst` | Liệt kê thư mục — `LIST` có loại (DIR/FILE) + kích thước, `NLST` chỉ có tên |
| `handleStou` | Giống STOR nhưng Server **tự đặt tên file** theo timestamp, cũng báo cổng qua `appendPortIfNeeded` |
| `handleAppe` | Giống STOR nhưng mở file bằng `ios::app` (nối vào cuối) |
| `handleDele` | Xóa 1 file |
| `handleRnfr`/`handleRnto` | Đổi tên 2 bước: `RNFR` lưu tên cũ, `RNTO` thực sự `fs::rename` |
| `handleHash` | Tính SHA-256 của file (gọi `HashUtil.cpp`) |
| `handlePort` | Parse cú pháp `h1,h2,h3,h4,p1,p2` → lưu IP/port ACTIVE của Client |
| `handlePasv` | Server tự chọn 1 cổng tuần tự trong dải 6000–6999 (`atomic<unsigned short>`, thread-safe), lấy IP thật qua `getsockname()`, trả mã `227` |
| `handleAbor` | Gửi `225` **trước** rồi mới gọi `Session::abortActiveTransfer()` — để Client luôn thấy `225` trước `426` |
| `handleQuit` | **Đợi `transferThread` xong hẳn** rồi mới trả `221 Goodbye` — tránh mất phản hồi cuối vì đóng socket quá sớm |
| `handle()` | Hàm điều phối trung tâm — chuyển `command` sang enum, `switch` gọi đúng hàm xử lý |

### 4.5. `RdtPacket.h` / `RdtPacket.cpp` — định dạng gói tin RDT (xem mục 2.1)

### 4.6. `DataChannel.h` / `DataChannel.cpp` — kênh dữ liệu UDP + RDT Go-Back-N (xem mục 2.2–2.4)

### 4.7. `HashUtil.h` / `HashUtil.cpp` — tính SHA-256 (không đổi)
- Dùng **BCrypt API** của Windows (thư viện hệ điều hành có sẵn từ Vista, không cần cài thêm).
- Luồng gọi: `BCryptOpenAlgorithmProvider` → `BCryptCreateHash` → `BCryptHashData` (nạp toàn bộ file) → `BCryptFinishHash` (32 byte) → `BCryptDestroyHash`/`BCryptCloseAlgorithmProvider`.
- Chuyển 32 byte nhị phân thành chuỗi hex 64 ký tự (thường) trả về qua lệnh `HASH`.

### 4.8. `Server.cpp` — hàm `main()`
`WSAStartup` → tạo thư mục `server_root` → khởi tạo và chạy `ControlChannel` → `control.run()` (chạy mãi) → `WSACleanup`.

---

## 5. CLIENT — giải thích từng file

### 5.1. `lib.h`
Tương tự Server nhưng gọn hơn. Chỉ còn `CONTROL_PORT = 8080`, `CHUNK_SIZE = 1024`. `enum class ClientDataMode` tương đương `DataMode` bên Server.

### 5.2. `RdtPacket.h` / `RdtPacket.cpp` — **giống hệt Server** (xem mục 2.1).

### 5.3. `DataChannel.h` / `DataChannel.cpp` — **giống hệt Server** (xem mục 2.2–2.4).

### 5.4. `ControlChannel.h` / `ControlChannel.cpp` — kênh điều khiển phía Client

Kiến trúc **2-thread**:
- **Thread chính**: đọc bàn phím (`std::getline(cin, input)`), tách `cmdWord`/`cmdArg` theo **dấu cách đầu tiên** (không giả định độ dài lệnh cố định — tránh lỗi cũ từng cắt sai tham số của các lệnh 3 ký tự như `MKD`, `CWD`, `PWD`, `RMD`). Nếu lệnh là `PORT`, lưu lại cổng mình sẽ tự bind. **Nếu lệnh là `RETR` mà `dataMode` đang `NONE`, tự động gọi `autoNegotiateActivePort()` trước** khi gửi lệnh thật. Trước khi gửi lệnh, lưu "lệnh đang chờ" (`pendingCmdWord`/`pendingArg`, bảo vệ bằng `mutex`) để thread nền biết cần làm gì khi thấy mã `150`.
- **Thread nền (`receiverLoop()`)**: liên tục `recv()` mọi phản hồi từ Server. Thấy mã `227` (PASV) → parse cổng PASSIVE của Server. Thấy mã `150` → đọc `" PORT=<n>"` nếu có (`parseEmbeddedPort`) để cập nhật `serverUploadPort`, rồi tự động gọi `doDataTransfer()` để truyền file qua UDP — **block thread nền, không block thread chính**, nên vẫn gõ được `ABOR` giữa lúc file đang truyền.
- `doDataTransfer(cmdWord, filename)`:
  - `STOR/STOU/APPE` → Client gửi file: chọn cổng đích là cổng PASSIVE của Server (nếu PASSIVE) hoặc `serverUploadPort` (cổng OS cấp Server vừa báo, nếu ACTIVE/NONE).
  - `RETR` → 2 trường hợp: **PASSIVE** thì gửi probe trước rồi `receiveFile()`; **ACTIVE** (kể cả tự động qua `autoNegotiateActivePort()`) thì mở `DataChannel` đúng cổng mình đã báo qua `PORT` rồi chờ nhận. Trường hợp còn lại (không có PORT/PASV nào) giờ chỉ là lớp bảo vệ, in lỗi chứ không đoán bừa cổng cố định như trước.
- `parsePortArgLocal()`/`parsePasvReply()`/`parseEmbeddedPort()`: parse 3 dạng chuỗi khác nhau — cú pháp `PORT` người dùng gõ, phản hồi `227` của Server, và `" PORT=<n>"` nhúng trong `150`.
- `autoNegotiateActivePort()`: bind thử 1 UDP socket tạm với `port=0` để hỏi OS cổng còn trống (`getsockname()`), lấy IP cục bộ qua chính socket điều khiển, gửi ngầm lệnh `PORT h1,h2,h3,h4,p1,p2` tới Server, cập nhật `dataMode = ACTIVE` ngay (không đợi phản hồi `200`, vì thứ tự gửi TCP đảm bảo `RETR` gửi sau đó chắc chắn tới Server sau lệnh `PORT` ngầm này).
- Destructor: đặt `keepRunning=false` và `join()` `receiverThread` để thoát chương trình sạch sẽ.

### 5.5. `Client.cpp` — hàm `main()`
`WSAStartup` → tạo `ControlChannel` với IP `"127.0.0.1"` → `start()` (connect TCP) → `run()` (vòng lặp bàn phím + thread nền) → `stop()` → `WSACleanup`.

---

## 6. Vài điểm kỹ thuật đáng học từ project này

1. **Tách "điều khiển" và "dữ liệu"** ra 2 kênh khác nhau (TCP cho lệnh, UDP cho file) đúng kiến trúc FTP thật — lệnh như `ABOR` không bao giờ bị kẹt phía sau một luồng dữ liệu đang truyền chậm.
2. **Go-Back-N + AIMD tự cài trên UDP** là bước đệm tự nhiên giữa Stop-and-Wait đơn giản và TCP thật: cửa sổ trượt (`window`) cho phép nhiều gói "bay" cùng lúc thay vì chờ từng gói, còn AIMD (`window+1` khi thành công, `window/2` khi timeout) chính là cơ chế Congestion Avoidance của TCP Reno đời đầu. Nếu bị hỏi "vì sao không dùng Selective Repeat" — trade-off đúng như giáo trình: Go-Back-N đơn giản hơn (receiver không cần buffer gói ngoài thứ tự), đổi lại phải gửi lại nhiều hơn khi mất gói.
3. **Một timer duy nhất canh cho gói `base`** (không phải mỗi gói 1 timer) là cách cài Go-Back-N chuẩn theo sách Kurose & Ross.
4. **Cổng "well-known" (8080, cố định) vs cổng ephemeral (OS cấp ngẫu nhiên)** là khái niệm networking thật — đúng như DNS dùng cổng 53 cố định còn client luôn dùng cổng ephemeral ở tầng transport. Việc thương lượng cổng qua kênh điều khiển giải quyết đúng vấn đề "nhiều Client cùng máy đụng cổng".
5. **Mỗi Client một Session, mỗi lệnh truyền file một thread phụ** — mô hình concurrent server có xử lý khá cẩn thận đồng bộ hóa (mutex, atomic, thứ tự hủy đối tượng) mà nhiều đồ án hay bỏ sót (double-free, race condition, mất phản hồi cuối vì đóng socket quá sớm — cả hai đều được sửa và giải thích ngay trong comment code).
6. **`resolvePath()` với `lexically_normal()` + `weakly_canonical()`** là kỹ thuật chuẩn chống path traversal — đáng nhớ cho bất kỳ server nào cho phép người dùng chỉ định đường dẫn.

---

*Tài liệu này giải thích code hiện có trong project (bản `Socket_fixed.zip`), không đánh giá đúng/sai theo rubric đồ án. Nếu bạn có file đề/rubric cụ thể (đặc biệt là công thức AIMD hoặc ngưỡng cửa sổ đề bài yêu cầu), gửi cho mình để đối chiếu xem code đã khớp yêu cầu chưa.*
