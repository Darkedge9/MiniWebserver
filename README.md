# 🚀 Mini C++ Multithreaded Web Server + Live Dashboard  
A lightweight, high-performance HTTP web server written in modern **C++17**, featuring:

- **Multi-threaded request handling** (thread pool)
- **Static file serving**
- **LRU file cache for high performance**
- **Security features** (path normalization, directory traversal protection)
- **Real-time Admin Dashboard** with request logs, charts, RPS, uptime, and connection metrics
- **JSON Metrics API** (`/admin/metrics`)
- **Health Check Endpoint** (`/admin/health`)

This project is designed as both a **learning tool** and a **production-inspired system**, showcasing networking, concurrency, HTTP fundamentals, caching, and real-time system monitoring.

----------------------------------------------------------------------

## 📸 Dashboard Preview
<img width="1914" height="903" alt="image" src="https://github.com/user-attachments/assets/98a2879e-2ca5-478a-8d18-f7f2560c10e9" />


![Dashboard Screenshot](assets/dashboard.png)

------------------------------------------------------------------

# 📂 Project Structure

mini_webserver/
├── src/
│ ├── server.cpp # Main server logic + admin API
│ ├── threadpool.cpp # ThreadPool for concurrent workers
│ ├── http_parser.cpp # Parses raw HTTP requests
│ ├── router.cpp # Maps URLs → files (static serving)
│ └── cache.cpp # LRU cache for file contents
├── include/
│ ├── server.h
│ ├── threadpool.h
│ ├── http_parser.h
│ ├── router.h
│ └── cache.h
├── static/
│ ├── index.html # Default home page
│ └── admin/
│ └── dashboard.html # Live monitoring dashboard
└── README.md


---

# ✨ Features

### 🧵 **1. Multi-threaded Web Server**
- Uses a custom **ThreadPool**
- Each incoming connection is processed in a worker thread
- Main thread only handles `accept()`

### 📄 **2. Static File Serving**
- Serves HTML, CSS, JS, images, and any static content from `./static/`
- Automatically handles:
  - `/` → `index.html`
  - MIME type detection

### ⚡ **3. LRU Cache**
- Stores frequently accessed files in memory
- Greatly reduces disk reads
- Cache size configurable

### 🔐 **4. Security**
- Blocks directory traversal (e.g., `/../../etc/passwd`)
- Normalizes file paths using `std::filesystem`

### 📊 **5. Real-Time Dashboard**
Live web dashboard at: http://localhost:8080/admin/dashboard.html


Includes:

- Total requests
- Uptime
- Requests per second (RPS)
- Active/peak connections
- Status code distribution (200/404/500/etc.)
- Bytes sent
- Recent request table
- Beautiful bar chart (Chart.js)

### 🔎 **6. Admin & Monitoring Endpoints**

| Endpoint | Purpose |
|---------|---------|
| `/admin/metrics` | Returns JSON metrics for the dashboard |
| `/admin/dashboard.html` | Interactive real-time UI |
| `/admin/health` | Returns "OK" (useful for uptime monitors) |

----------------------------------------------------------------

# 🧠 Architecture Overview

### 🔹 High-level flow

Browser → HTTP Request → accept() → ThreadPool → handle_client() → parse_http_request() → router → cache → generate HTTP response → send() → log_request() → dashboard updates


### 🔹 Modules

| Module | Responsibility |
|--------|----------------|
| `HttpServer` | Socket handling, admin endpoints, request lifecycle |
| `ThreadPool` | Multi-threaded task execution |
| `HttpParser` | Converts raw HTTP into structured data |
| `Router` | Maps URLs to static files, determines content type |
| `LRUCache` | Speeds up repeated file accesses |
| Dashboard | Periodically fetches `/admin/metrics` and visualizes stats |

---

# 🛠 Requirements

### ✔ Linux / WSL / macOS  
(Works best under Linux or WSL because it uses POSIX sockets.)

### ✔ C++17 or newer

Install build essentials:

```bash
sudo apt update
sudo apt install g++ build-essential

------------------------------------------------------------------------
🚀 Build & Run
1️⃣ Clone the repo
git clone https://github.com/<your-username>/mini_webserver.git
cd mini_webserver

2️⃣ Build the server
g++ -std=c++17 src/*.cpp -Iinclude -pthread -o mini_webserver

3️⃣ Run the server
./mini_webserver 8080 4 ./static


