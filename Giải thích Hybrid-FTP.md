# Giải thích chi tiết project SOCKET — Hybrid FTP Client/Server (C++ / Winsock)

## 0. Bức tranh tổng thể

Đây là một **FTP client/server tự viết**, mô phỏng đúng kiến trúc FTP thật (RFC 959):

- **Kênh điều khiển (Control Channel) — TCP, cổng 8080**: nơi Client gửi lệnh dạng text (`USER`, `PASS`, `STOR`, `RETR`, ...) và Server trả về mã phản hồi kiểu FTP (`220`, `226`, `550`, ...). Kênh này luôn mở suốt phiên làm việc.
- **Kênh dữ liệu (Data Channel) — UDP**: nơi truyền nội dung file thật sự. Vì UDP không đảm bảo gói tin đến nơi, đến đủ, đến đúng thứ tự, project tự cài một giao thức tin cậy gọi là **RDT (Reliable Data Transfer)** theo mô hình **Stop-and-Wait ARQ** (kiến thức lõi trong môn Mạng máy tính — giống hệt cơ chế RDT 3.0 trong sách Kurose & Ross).

Mỗi Client kết nối vào Server sẽ được Server tạo một **thread riêng** để xử lý (mô hình thread-per-connection), và trong thread đó, khi có lệnh truyền file (STOR/RETR/...), Server lại tách ra **một thread phụ khác** chỉ để chạy việc truyền dữ liệu UDP — nhờ vậy luồng chính vẫn rảnh để nhận lệnh mới (kể cả lệnh `ABOR` để hủy transfer đang chạy).

Cấu trúc thư mục:
```
SERVER - 7/SERVER/   → chương trình Server
CLIENT - 7/CLIENT/   → chương trình Client
```

Một điều thú vị: file `RdtPacket.h`, `RdtPacket.cpp` và `DataChannel.cpp` ở **Client và Server giống hệt nhau (byte-by-byte)**. Điều này hợp lý vì giao thức RDT là đối xứng — cả hai bên đều cần gửi được và nhận được theo cùng một luật.

---

## 1. Luồng hoạt động end-to-end

### 1.1. Bắt tay kết nối
1. Server (`Server.cpp`) khởi động Winsock, tạo thư mục `server_root/` (gốc lưu file của mọi client), mở `ControlChannel` lắng nghe TCP cổng 8080.
2. Client (`Client.cpp`) khởi động Winsock, `connect()` tới `127.0.0.1:8080`.
3. Server `accept()` được kết nối → tạo `std::thread` riêng chạy `ControlChannel::handleClient()`, gửi `"220 Service ready"`.
4. Trong thread đó, Server tạo một `Session` (trạng thái riêng của client này: đã đăng nhập chưa, đang ở thư mục nào, ACTIVE hay PASSIVE...) và một `CommandHandler` (bộ xử lý lệnh).

### 1.2. Vòng lặp lệnh
- Client: thread chính đọc bàn phím (`ftp> ...`), gửi câu lệnh qua TCP; một **thread nền riêng** (`receiverThread`) liên tục `recv()` phản hồi từ Server. Tách hai thread này ra là để bạn vẫn gõ được `ABOR` trong lúc một lệnh khác (như RETR) đang chờ dữ liệu — nếu dùng chung 1 thread, `recv()` chờ dữ liệu sẽ chặn luôn bàn phím.
- Server: mỗi lần `recv()` được 1 dòng lệnh, `parseCmd()` tách ra `command` + `argument`, rồi `CommandHandler::handle()` định tuyến (dispatch) tới đúng hàm xử lý (`handleUSER`, `handleSTOR`, ...) dựa trên `enum class FtpCommand`.

### 1.3. Khi có lệnh truyền file (STOR/RETR/STOU/APPE)
Đây là phần "hybrid" — TCP điều phối, UDP truyền dữ liệu thật:
1. Server nhận lệnh, mở một `DataChannel` (UDP), bind cổng.
2. Server trả `"150 File status okay, opening data connection"` qua TCP.
3. Server **tách một thread phụ** (`transferThread`) để thực sự đọc/ghi file qua UDP+RDT — thread chính quay lại nhận lệnh mới ngay lập tức.
4. Client thấy phản hồi `"150 ..."` trong `receiverLoop()` → tự động gọi `doDataTransfer()` để mở `DataChannel` phía mình và bắt đầu gửi/nhận qua UDP.
5. Khi xong, thread phụ của Server gửi tiếp `"226 Transfer complete"` (hoặc `"426 ..."` nếu lỗi/bị hủy) qua TCP.

### 1.4. ACTIVE vs PASSIVE (giống FTP thật)
- **ACTIVE**: Client tự chọn cổng UDP của mình, báo cho Server qua lệnh `PORT h1,h2,h3,h4,p1,p2` → khi truyền, **Server chủ động gửi tới** địa chỉ đó.
- **PASSIVE**: Client gửi `PASV` → Server tự chọn 1 cổng, mở sẵn, trả về địa chỉ qua mã `227` → **Client chủ động kết nối tới** Server. Vì UDP không có handshake như TCP, khi Server cần *gửi* file cho Client ở chế độ PASSIVE (lệnh RETR), Server chưa biết chính xác địa chỉ nguồn của Client, nên phải chờ Client gửi một **gói "probe"** nhỏ trước (`sendProbe`) để "học" được địa chỉ (`sendFileAfterHandshake`) — đây là kỹ thuật NAT traversal đơn giản kiểu UDP hole punching thu nhỏ.

### 1.5. Cơ chế RDT (Stop-and-Wait ARQ) — trái tim của kênh dữ liệu
Vì UDP có thể mất gói, trùng gói, hoặc hỏng dữ liệu, project tự đóng gói mỗi lần truyền theo cấu trúc `RdtPacket`:

```
[seqNum 4B][flags 1B][checksum 2B][payloadLength 2B][payload...]
```

- **flags**: `FLAG_DATA` (mang dữ liệu), `FLAG_ACK` (xác nhận), `FLAG_FIN` (báo kết thúc).
- **seqNum**: chỉ có giá trị 0 hoặc 1, đổi luân phiên (0↔1) sau mỗi gói — đúng chuẩn Stop-and-Wait (không cần cửa sổ trượt lớn vì tại một thời điểm chỉ có 1 gói "đang bay").
- **checksum**: kiểu Internet checksum (1's complement sum, giống checksum trong header IP/TCP/UDP thật) để phát hiện lỗi bit.

Vòng lặp gửi (`rdtSend`): chia file thành các chunk 1024B → gửi chunk → chờ ACK đúng `seqNum` trong `RDT_TIMEOUT_MS` (500ms) → nếu timeout thì gửi lại (tối đa `RDT_MAX_RETRIES` = 20 lần) → khi ACK đúng thì đảo `seqNum` và chuyển chunk kế → hết dữ liệu thì gửi gói `FIN` và cũng chờ ACK cho `FIN`.

Vòng lặp nhận (`rdtReceive`): nhận gói → kiểm tra checksum (sai thì **âm thầm bỏ, không ACK** — buộc bên gửi phải gửi lại) → nếu là `FIN` thì ACK rồi kết thúc → nếu là `DATA` đúng `expectedSeq` thì ghi nhận dữ liệu và đảo `expectedSeq`, còn nếu là gói trùng (do ACK trước bị mất nên bên gửi gửi lại) thì **vẫn ACK nhưng không ghi dữ liệu lần hai** (tránh dữ liệu bị lặp).

File còn có cơ chế **`SIMULATE_PACKET_LOSS`** để giả lập mất gói ngẫu nhiên (10%) — dùng để test khả năng chịu lỗi của RDT khi làm báo cáo/demo.

---

## 2. SERVER — giải thích từng file

### 2.1. `lib.h` — file cấu hình & thư viện dùng chung
- Include toàn bộ thư viện cần: `<filesystem>` (thao tác thư mục/file kiểu hiện đại C++17), `<thread>`/`<mutex>`/`<atomic>` (đa luồng), `<winsock2.h>` (socket Windows).
- Khai báo hằng số cổng: `CONTROL_PORT=8080` (TCP lệnh), `SERVER_DATA_PORT=8081` (UDP Server nghe khi nhận STOR), `CLIENT_DATA_PORT=8082` (UDP Client nghe khi RETR mặc định), `CHUNK_SIZE=1024`.
- `SERVER_ROOT`: thư mục gốc vật lý trên đĩa, mọi đường dẫn "ảo" của Client (`/`, `/abc`) đều được ánh xạ (map) vào bên trong thư mục này — đây chính là cơ chế **sandbox**, ngăn Client dùng `../../..` để đọc file ngoài phạm vi cho phép.
- `g_coutMutex`: mutex toàn cục để tránh nhiều thread (nhiều Client) in đè lên nhau trên console.
- `enum class DataMode { NONE, ACTIVE, PASSIVE }` và `enum class FtpCommand {...}`: dùng enum thay vì so sánh chuỗi liên tục, giúp code nhanh và ít lỗi gõ nhầm hơn.

### 2.2. `Session.h` / `Session.cpp` — trạng thái riêng của mỗi Client
Mỗi kết nối Client có **một `Session` độc lập** (không dùng biến toàn cục), lưu:
- `isLoggedIn`, `userName`: trạng thái đăng nhập (đăng nhập ở đây chỉ mang tính hình thức, không kiểm tra mật khẩu thật).
- `currentDir`: thư mục "ảo" hiện tại (ví dụ `/photos`).
- `dataType` (A/I), `transferMode` (S/B/C — nhưng chỉ hỗ trợ S): mô phỏng lệnh `TYPE`/`MODE` của FTP chuẩn.
- `renameFrom`: lưu tạm tên file khi gõ `RNFR` chờ `RNTO` hoàn tất (rename cần 2 bước).
- `dataMode`, `activeIp/activePort`, `passivePort`: trạng thái ACTIVE/PASSIVE.
- `activeDataChannel`: **con trỏ quan sát** (observer pointer, không sở hữu) tới `DataChannel` đang chạy transfer, chỉ để lệnh `ABOR` có thể gọi `stop()` đóng socket từ một thread khác.

Điểm đáng chú ý nhất trong file này là phần comment giải thích **lỗi double-free đã từng gặp và cách sửa**: ban đầu `setActiveDataChannel(nullptr)` từng gọi `delete` con trỏ đó, nhưng vì đối tượng `DataChannel` thật sự được quản lý bởi một `shared_ptr` khác nằm trong lambda của `transferThread`, nên khi `shared_ptr` đó tự hủy ở cuối lambda, nó sẽ `delete` chính đối tượng đã bị `delete` trước đó → crash/heap corruption khó tái hiện. Cách sửa: hàm này **không bao giờ `delete`**, chỉ lưu/xóa địa chỉ con trỏ.

`abortActiveTransfer()`: được `mutex` bảo vệ, gọi `activeDataChannel->stop()` — hàm `stop()` sẽ `closesocket()` khiến `recvfrom()`/`sendto()` đang bị block ở thread phụ lập tức trả về lỗi, nhờ đó thread phụ biết để thoát vòng lặp.

### 2.3. `ControlChannel.h` / `ControlChannel.cpp` — kênh điều khiển TCP
- `start()`: tạo socket TCP (`SOCK_STREAM`), `bind()` vào `INADDR_ANY:8080`, `listen()`.
- `run()`: vòng lặp `accept()` **vô hạn** — mỗi Client mới kết nối sẽ được `accept()` trả về 1 socket riêng, rồi `thread(...).detach()` tạo thread mới xử lý Client đó và **tách rời** (detach) khỏi thread chính, để thread chính quay lại `accept()` Client tiếp theo ngay.
- `handleClient(clientSocket, clientIp)`: đây là "trái tim" xử lý một phiên làm việc trọn vẹn của 1 Client — gửi `220`, khởi tạo `Session` + `CommandHandler`, vòng lặp `recv()` lệnh → `parseCmd()` → `handler.handle()` → `send()` phản hồi, cho tới khi Client ngắt kết nối hoặc gõ `QUIT`.
- `stop()`: đóng socket lắng nghe.

### 2.4. `CmdHandler.h` / `CmdHandler.cpp` — bộ não xử lý lệnh FTP (file lớn nhất)

**Hàm tiện ích ở đầu file:**
- `parseCmd(raw, cmd, arg)`: cắt bỏ `\r\n`, tách token đầu tiên làm `command`, phần còn lại làm `argument`, và viết hoa `command` (FTP command không phân biệt hoa/thường).
- `toFtpCommand(cmd)`: chuyển chuỗi lệnh (`"STOR"`) sang `enum FtpCommand::STOR` để `switch-case` nhanh và rõ ràng hơn `if-else` chuỗi.
- `resolvePath(session, arg, outLogical)`: **hàm quan trọng nhất về bảo mật** — nhận đường dẫn Client gửi (có thể tương đối hoặc tuyệt đối), chuẩn hóa bằng `lexically_normal()` (xử lý `..`, `.`), rồi map sang đường dẫn vật lý thật bên trong `SERVER_ROOT` bằng `weakly_canonical()`. Nhờ `lexically_normal()` không cho `..` vượt quá gốc, Client **không thể** dùng `CWD ../../../Windows` để thoát ra khỏi `server_root`.
- `pickListenPort(session)`: nếu đang PASSIVE thì Server nghe đúng cổng đã hẹn trước qua `PASV`; nếu không thì dùng cổng cố định `SERVER_DATA_PORT` (8081).
- `joinPreviousTransfer()`: trước khi bắt đầu 1 lệnh mới, luôn `join()` chờ `transferThread` cũ (nếu còn) hoàn tất — tránh 2 transfer chạy chồng lên nhau trên cùng 1 Session.

**Các hàm `handleXxx` — mỗi hàm ứng với đúng 1 lệnh FTP**, đa số theo khuôn: kiểm tra đăng nhập → kiểm tra tham số → xử lý → trả mã phản hồi kiểu `"2xx/4xx/5xx <message>\r\n"` (đúng chuẩn RFC 959). Điểm đáng chú ý:

| Hàm | Việc làm chính |
|---|---|
| `handleUser`/`handlePass` | Lưu username, đặt `isLoggedIn=true` (không kiểm tra mật khẩu thật — mô phỏng đơn giản) |
| `handlePwd` | Trả về `currentDir` |
| `handleType`/`handleMode` | Chỉ chấp nhận `TYPE {A,I}` và `MODE S` (giản lược so với FTP đầy đủ) |
| `handleSize`/`handleMdtm`/`handleStat` | Truy vấn `fs::file_size`, `fs::last_write_time` để trả kích thước/thời gian sửa/tình trạng |
| **`handleStor`** | Nhận file từ Client: mở `DataChannel` UDP, trả `150`, rồi **tách thread phụ** gọi `dc->receiveFile()`; xong thì gửi `226`/`426` |
| **`handleRetr`** | Gửi file cho Client: tùy ACTIVE/PASSIVE mà chọn cổng nghe và địa chỉ đích khác nhau, cũng chạy trong thread phụ |
| `handleCwd`/`handleCdup` | Đổi `currentDir` sau khi xác nhận thư mục đích tồn tại |
| `handleMkd`/`handleRmd` | Tạo/xóa thư mục (RMD chỉ xóa được thư mục **rỗng**, dùng `fs::remove` chứ không phải `remove_all`) |
| `handleList`/`handleNlst` | Liệt kê thư mục — `LIST` có thêm loại (DIR/FILE) và kích thước, `NLST` chỉ có tên |
| `handleStou` | Giống STOR nhưng Server **tự đặt tên file** theo timestamp (`file_<ms>.dat`), không dùng tên Client gửi |
| `handleAppe` | Giống STOR nhưng mở file bằng `ios::app` (nối vào cuối thay vì ghi đè) |
| `handleDele` | Xóa 1 file |
| `handleRnfr`/`handleRnto` | Đổi tên 2 bước: `RNFR` lưu tên cũ vào `Session::renameFrom`, `RNTO` mới thực sự `fs::rename` |
| `handleHash` | Tính SHA-256 của file (gọi `HashUtil.cpp`) để Client xác thực tính toàn vẹn sau khi tải xong |
| `handlePort` | Parse cú pháp `h1,h2,h3,h4,p1,p2` (đúng chuẩn FTP PORT) → lưu IP/port ACTIVE của Client |
| `handlePasv` | Server tự chọn 1 cổng tuần tự trong dải 6000–6999 (dùng `atomic<unsigned short>` để nhiều Client gọi PASV cùng lúc vẫn an toàn, không đụng cổng nhau), lấy IP thật của Server qua `getsockname()`, trả về mã `227` |
| `handleAbor` | Gửi `225` **trước** rồi mới gọi `Session::abortActiveTransfer()` — thứ tự này quan trọng để Client luôn thấy `225` trước khi thấy `426` (do thread phụ tự gửi khi phát hiện socket bị đóng) |
| `handleQuit` | **Đợi `transferThread` xong hẳn** (`joinPreviousTransfer()`) rồi mới trả `221 Goodbye` — sửa lỗi cũ: nếu đóng socket ngay trong khi transfer còn chạy, `226`/`426` gửi sau đó sẽ bị mất vì socket đã đóng |
| `handle()` | Hàm điều phối trung tâm — nhận `command` dạng string, chuyển sang enum, `switch` gọi đúng hàm xử lý |

### 2.5. `RdtPacket.h` / `RdtPacket.cpp` — định dạng gói tin RDT
(Giống hệt bên Client — xem mục 3.2 vì không lặp lại.)

### 2.6. `DataChannel.h` / `DataChannel.cpp` — kênh dữ liệu UDP + RDT
(Giống hệt bên Client — xem mục 3.3.)

### 2.7. `HashUtil.h` / `HashUtil.cpp` — tính SHA-256
- Dùng **BCrypt API** của Windows (thư viện hệ điều hành có sẵn từ Windows Vista, không cần cài thêm gì) thay vì tự viết thuật toán SHA-256 hay dùng thư viện ngoài (OpenSSL...).
- Luồng gọi API: `BCryptOpenAlgorithmProvider` (mở "nhà cung cấp" thuật toán SHA-256) → `BCryptCreateHash` (tạo đối tượng hash) → `BCryptHashData` (nạp toàn bộ nội dung file vào) → `BCryptFinishHash` (lấy ra 32 byte kết quả) → `BCryptDestroyHash`/`BCryptCloseAlgorithmProvider` (dọn dẹp).
- Cuối cùng chuyển 32 byte nhị phân thành chuỗi hex 64 ký tự (viết thường) để trả về qua lệnh `HASH`.

### 2.8. `Server.cpp` — hàm `main()`
`WSAStartup` → tạo thư mục `server_root` → khởi tạo và chạy `ControlChannel` → `control.run()` (chạy mãi cho đến khi bị dừng) → `WSACleanup`.

---

## 3. CLIENT — giải thích từng file

### 3.1. `lib.h` — tương tự bên Server nhưng gọn hơn (không cần `<filesystem>` cho sandbox, không cần mutex bảo vệ nhiều Client vì Client chỉ có 1 phiên). Có thêm `enum class ClientDataMode` tương đương `DataMode` bên Server.

### 3.2. `RdtPacket.h` / `RdtPacket.cpp` — **giống hệt Server**, vì đây là "ngôn ngữ chung" cả hai bên phải hiểu như nhau:
- `computeChecksum()`: cộng từng cặp 2 byte theo big-endian vào tổng 32-bit, gói lẻ thì pad thêm byte 0, "fold carry" (cộng phần tràn 16-bit cao vào 16-bit thấp) rồi đảo bit — đúng thuật toán Internet checksum dùng trong IP/TCP/UDP mà môn Mạng máy tính có dạy.
- `verifyChecksum()`: tính checksum trên **toàn bộ** gói tin (bao gồm cả field checksum đã có giá trị) — nếu không lỗi thì kết quả 1's complement phải ra toàn số 1 (tức đảo bit ra 0).
- `serializePacket()`: đóng gói `RdtPacket` (struct trong bộ nhớ) thành mảng byte thô để gửi qua mạng — ghi `seqNum` theo network byte order (`htonl`), rồi `flags`, rồi checksum tạm =0, rồi `payloadLength` (`htons`), rồi tính checksum thật trên toàn buffer và ghi đè lại vào đúng vị trí.
- `deserializePacket()`: làm ngược lại — đọc header, kiểm tra `payloadLength` khớp với số byte thực nhận, kiểm tra checksum, rồi mới lấy `payload`. Nếu bất kỳ bước nào sai → trả `false` (gói bị hỏng, sẽ bị bên nhận âm thầm bỏ qua).
- `shouldSimulateLoss()`: dùng `thread_local` random engine (mỗi thread có bộ sinh số ngẫu nhiên riêng, tránh tranh chấp dữ liệu) để giả lập rớt gói khi bật `SIMULATE_PACKET_LOSS`.

### 3.3. `DataChannel.h` / `DataChannel.cpp` — **giống hệt Server**:
- `start()`: tạo UDP socket, bind vào `udpPort` (0 nghĩa là để hệ điều hành tự chọn cổng ephemeral).
- `rdtSend()`: **Phase 1** — chia dữ liệu thành từng chunk ≤1024B, với mỗi chunk: serialize → `sendto()` → đặt timeout 500ms chờ `recvfrom()` ACK → nếu đúng seq thì qua chunk tiếp, nếu timeout thì gửi lại (tối đa 20 lần), nếu ACK sai/hỏng thì bỏ qua và tiếp tục chờ. **Phase 2** — sau khi hết dữ liệu, gửi gói `FIN` theo đúng cơ chế tương tự để báo kết thúc.
- `rdtReceive()`: vòng lặp `recvfrom()` liên tục, timeout dài hơn nhiều (`RDT_TIMEOUT_MS * (RDT_MAX_RETRIES+1)` vì bên gửi có thể đang bận retransmit); mỗi gói nhận được: kiểm tra checksum (sai thì DROP không ACK), nếu là FIN thì ACK và dừng, nếu là DATA đúng seq thì append vào buffer kết quả và đảo `expectedSeq`, nếu là DATA trùng (do ACK cũ bị mất khiến bên kia gửi lại) thì vẫn ACK nhưng **không** ghi dữ liệu lần 2 (tránh double-write).
- `receiveFile()`/`sendFile()`: bọc quanh `rdtReceive`/`rdtSend` để đọc/ghi vào file thật trên đĩa (`ifstream`/`ofstream` ở chế độ `ios::binary`).
- `sendFileAfterHandshake()` (chỉ dùng ở Server khi RETR+PASSIVE): chờ nhận 1 gói "probe" qua RDT để học địa chỉ IP:port thật của Client rồi mới `sendFile()` tới đúng địa chỉ đó.
- `sendProbe()` (chỉ dùng ở Client khi RETR+PASSIVE): gửi 1 byte `'R'` qua RDT để Server học được địa chỉ của mình.
- `stop()`: dùng `atomic<SOCKET>::exchange()` để đảm bảo chỉ đúng 1 thread thực sự đóng socket dù có nhiều thread cùng gọi `stop()` (ví dụ cả luồng transfer tự kết thúc lẫn `ABOR` cùng gọi).

### 3.4. `ControlChannel.h` / `ControlChannel.cpp` — kênh điều khiển phía Client (phần khác biệt lớn nhất so với Server)

Đây là nơi thể hiện rõ nhất kiến trúc **2-thread** phía Client:
- **Thread chính**: đọc bàn phím (`std::getline(cin, input)`), tách `cmdWord`/`cmdArg` theo **dấu cách đầu tiên** (không giả định độ dài cố định — file có ghi rõ lỗi cũ: dùng `substr(0,4)`/`substr(5)` từng làm sai với các lệnh 3 ký tự như `MKD`, `CWD`, `PWD`, `RMD`, ví dụ `"MKD test"` bị cắt nhầm thành `"MKD est"`). Trước khi gửi lệnh, lưu lại "lệnh đang chờ" (`pendingCmdWord`/`pendingArg`, bảo vệ bằng `mutex`) để thread nền biết cần làm gì khi thấy mã `150`.
- **Thread nền (`receiverThread` chạy `receiverLoop()`)**: liên tục `recv()` mọi phản hồi từ Server. Khi thấy mã `227` (PASV) thì parse ra cổng PASSIVE của Server; khi thấy mã `150` thì tự động gọi `doDataTransfer()` để thực hiện việc truyền file qua UDP — **việc này block (chặn) thread nền**, nhưng **không** chặn thread chính, nên bạn vẫn gõ được `ABOR` giữa lúc file đang truyền.
- `doDataTransfer(cmdWord, filename)`: 
  - Nếu là `STOR/STOU/APPE` → Client gửi file: chọn cổng đích là cổng PASSIVE của Server (nếu đang PASSIVE) hoặc `SERVER_DATA_PORT` cố định (nếu ACTIVE/mặc định).
  - Nếu là `RETR` → 3 trường hợp: **PASSIVE** thì gửi probe trước rồi mới `receiveFile()`; **ACTIVE** thì mở `DataChannel` đúng cổng mình đã báo qua `PORT` rồi chờ nhận; **mặc định** (chưa từng gọi PORT/PASV) thì dùng cổng cố định `CLIENT_DATA_PORT`.
- `parsePortArgLocal()`/`parsePasvReply()`: parse ngược lại cú pháp `h1,h2,h3,h4,p1,p2` — một cái từ chính lệnh `PORT` người dùng gõ, một cái từ chuỗi phản hồi `227` của Server.
- Destructor: đặt `keepRunning=false` và `join()` `receiverThread` để đảm bảo thoát chương trình sạch sẽ, không có thread "mồ côi".

### 3.5. `Client.cpp` — hàm `main()`
Ngắn gọn: `WSAStartup` → tạo `ControlChannel` với IP `"127.0.0.1"` → `start()` (connect TCP) → `run()` (vòng lặp bàn phím + thread nền) → `stop()` → `WSACleanup`.

---

## 4. Vài điểm kỹ thuật đáng học từ project này

1. **Tách "điều khiển" và "dữ liệu"** ra 2 kênh khác nhau (TCP cho lệnh, UDP cho file) là đúng kiến trúc FTP thật — giúp gửi lệnh (như `ABOR`) không bao giờ bị kẹt phía sau một luồng dữ liệu đang truyền chậm.
2. **Stop-and-Wait ARQ tự cài trên UDP** là cách rất trực quan để hiểu cơ chế RDT trong giáo trình mạng máy tính: mọi thứ (mất gói, gói trùng, gói hỏng, timeout, retransmit) đều được xử lý tường minh trong code, không "ẩn" sau thư viện nào.
3. **Mỗi Client một Session, mỗi lệnh truyền file một thread phụ** — đúng mô hình concurrent server, và code có xử lý khá cẩn thận các vấn đề đồng bộ hóa (mutex, atomic, thứ tự hủy đối tượng) mà rất nhiều đồ án sinh viên hay bỏ sót (double-free, race condition, mất phản hồi cuối vì đóng socket quá sớm).
4. **`resolvePath()` với `lexically_normal()` + `weakly_canonical()`** là kỹ thuật chuẩn để chống path traversal — rất đáng nhớ nếu sau này làm bất kỳ server nào cho phép người dùng chỉ định đường dẫn.
5. **Internet checksum + ACK theo sequence 0/1** chính là mô hình "rdt3.0" kinh điển — nếu bạn học phần Transport Layer trong CTT105, đây là cách nhìn thấy lý thuyết chạy thành code thật.

---

*Tài liệu này giải thích code đã có sẵn trong project, không đánh giá đúng/sai theo rubric đồ án. Nếu bạn muốn mình review/chấm điểm theo tiêu chí cụ thể của đề bài, hãy gửi file đề/rubric để mình đối chiếu.*
