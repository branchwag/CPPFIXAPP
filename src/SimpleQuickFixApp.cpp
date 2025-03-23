#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <queue>
#include <mutex>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <quickfix/FileStore.h>
#include <quickfix/FileLog.h>
#include <quickfix/SocketInitiator.h>
#include <quickfix/SessionSettings.h>
#include <quickfix/Application.h>
#include <quickfix/MessageCracker.h>
#include <quickfix/fix44/NewOrderSingle.h>
#include <quickfix/fix44/ExecutionReport.h>

struct OrderRequest {
    std::string symbol;
    std::string side;
    double price;
    int quantity;
};

class FIXApplication : public FIX::Application, public FIX::MessageCracker {
private: 
    std::atomic<bool> connected; 
    std::atomic<bool> stopRequested;
    std::thread httpServerThread;
    int orderID;
    FIX::SessionID currentSession;

    std::queue<OrderRequest> orderQueue;
    std::mutex queueMutex;

public: 
    FIXApplication() : connected(false), stopRequested(false), orderID(0) {}

    ~FIXApplication() {
        stopServer();
    }
    
    void onCreate(const FIX::SessionID&) override {}
    
    void onLogon(const FIX::SessionID& sessionID) override {
        std::cout << "Logon - " << sessionID.toString() << std::endl;
        connected = true;
        currentSession = sessionID;
    }
    
    void onLogout(const FIX::SessionID& sessionID) override {
        std::cout << "Logout - " << sessionID.toString() << std::endl;
        connected = false;
    }
    
    void toAdmin(FIX::Message& message, const FIX::SessionID& sessionID) override {}
    
    void fromAdmin(const FIX::Message& message, const FIX::SessionID& sessionID) 
        throw(FIX::FieldNotFound, FIX::IncorrectDataFormat, FIX::IncorrectTagValue, FIX::RejectLogon) override {}
    
    void toApp(FIX::Message& message, const FIX::SessionID& sessionID)
        throw(FIX::DoNotSend) override {}
    
    void fromApp(const FIX::Message& message, const FIX::SessionID& sessionID) 
        throw(FIX::FieldNotFound, FIX::IncorrectDataFormat, FIX::IncorrectTagValue, FIX::UnsupportedMessageType) override {
        crack(message, sessionID);
    }

    void onMessage(const FIX44::ExecutionReport& message, const FIX::SessionID& sessionID) {
        FIX::ExecID execID;
        FIX::OrderID orderID;
        FIX::ExecType execType;
        FIX::OrdStatus ordStatus;
        FIX::Symbol symbol;

        message.get(orderID);
        message.get(execID);
        message.get(execType);
        message.get(ordStatus);
        message.get(symbol);

        std::cout << "Received execution report - OrderID: " << orderID
                  << ", ExecID: " << execID
                  << ", ExecType: " << execType
                  << ", OrdStatus: " << ordStatus
                  << ", Symbol: " << symbol << std::endl;
    }
    
    bool isConnected() const {
        return connected;
    }

    void startServer(int port) {
        stopServer(); //stop any existing thread
        
        stopRequested = false;
        httpServerThread = std::thread([this, port] () {
            runHttpServer(port);
        });
    }

    void stopServer() {
        stopRequested = true;
        if (httpServerThread.joinable()) {
            httpServerThread.join();
        }
    }

    void runHttpServer(int port) {
        int server_fd, new_socket;
        struct sockaddr_in address;
        int addrlen = sizeof(address);

        //socket file descriptor
        if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
            std::cerr << "Socket creation failed" << std::endl;
            return;
        }

        //set socket option to reuse address
        int opt = 1;
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
            std::cerr << "setsockopt failed" << std::endl;
            return;
        }

        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);
        
        //bind socket to net address and port
        if (bind(server_fd,(struct sockaddr *)&address, sizeof(address)) < 0) {
            std::cerr << "Bind failed" << std::endl;
            return;
        }

        //http server listen for incoming connections
        if (listen(server_fd, 3) < 0) {
            std::cerr << "Listen failed" << std::endl;
            return;
        }

        std::cout << "HTTP server listening on port " << port << std::endl;

        //set socket to non-blocking
        int flags = fcntl(server_fd, F_GETFL, 0);
        fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);

        while (!stopRequested) {
            processOrderQueue();

            new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
            if (new_socket < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    std::cerr << "Accept failed" << std::endl;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }


            char buffer[1024] = {0};
            int valread = read(new_socket, buffer, 1024);
            if (valread > 0) {
                std::string request(buffer);
                handleHttpRequest(new_socket, request);
            }

            close(new_socket);
        }
    
        close(server_fd);
    }

    void handleHttpRequest(int socket, const std::string& request) {
        //parse http request for order deets
        if (request.find("POST /api/order") != std::string::npos) {
            size_t bodyStart = request.find("\r\n\r\n");
            if (bodyStart != std::string::npos) {
                std::string body = request.substr(bodyStart + 4);
                OrderRequest order = parseOrderBody(body);
                
                //add order to queue
                std::lock_guard<std::mutex> lock(queueMutex);
                orderQueue.push(order);

                std::string response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n\r\n{\"status\":\"success\",\"message\":\"Order queued\"}";
                send(socket, response.c_str(), response.length(), 0);
                return;
            }
        } else if (request.find("OPTIONS") != std::string::npos) {
            //CORS preflight requests
            std::string response = "HTTP/1.1 200 OK\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: POST, GET, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type\r\n\r\n";
            send(socket, response.c_str(), response.length(), 0);
            return;
        }

        //404 otherwise
        std::string response = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nAccess-Control-Allow-Origin: *\r\n\r\nNot Found";
        send(socket, response.c_str(), response.length(), 0);
    }

    OrderRequest parseOrderBody(const std::string& body) {
        OrderRequest order;

        order.symbol = "ZVZZT";
        order.side = "BUY";
        order.price = 25.50;
        order.quantity = 100;

        //simple parsing to be later replaced by proper JSON parser
        size_t pos;

        if ((pos = body.find("\"symbol\":")) != std::string::npos) {
            size_t start = body.find("\"", pos + 9) + 1;
            size_t end = body.find("\"", start);
            if (start != std::string::npos && end != std::string::npos) {
                order.symbol = body.substr(start, end - start);
            }
        }

        if ((pos = body.find("\"side\":")) != std::string::npos) {
            size_t start = body.find("\"", pos + 7) + 1;
            size_t end = body.find("\"", start);
            if (start != std::string::npos && end != std::string::npos) {
                order.side = body.substr(start, end - start);
            }
        }
        
        if ((pos = body.find("\"price\":")) != std::string::npos) {
            size_t start = pos + 8;
            size_t end = body.find(",", start);
            if (end == std::string::npos) {
                end = body.find("}", start);
            }
            if (start != std::string::npos && end != std::string::npos) {
                try {
                    order.price = std::stod(body.substr(start, end - start));
                } catch (...) {
                    // Keep default on error
                }
            }
        }        
        
        if ((pos = body.find("\"quantity\":")) != std::string::npos) {
            size_t start = pos + 11;
            size_t end = body.find(",", start);
            if (end == std::string::npos) {
                end = body.find("}", start);
            }
            if (start != std::string::npos && end != std::string::npos) {
                try {
                    order.quantity = std::stoi(body.substr(start, end - start));
                } catch (...) {
                    // Keep default on error
                }
            }
        }
        
        return order;
    }
        
    void processOrderQueue() {
        if (!connected) return;

        OrderRequest order;
        bool hasOrder = false;

        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if (!orderQueue.empty()) {
                order = orderQueue.front();
                orderQueue.pop();
                hasOrder = true;
            }
        }

        if (hasOrder) {
            sendOrder(order);
        }
    }
    
    void sendOrder(const OrderRequest& order) {
        try {
            FIX44::NewOrderSingle newOrder;
            
            std::string clOrdID = "ORDER" + std::to_string(++orderID);
            newOrder.set(FIX::ClOrdID(clOrdID));
            
            newOrder.set(FIX::Symbol(order.symbol));

            if (order.side == "BUY") {
                newOrder.set(FIX::Side(FIX::Side_BUY));
            } else {
                newOrder.set(FIX::Side(FIX::Side_SELL));
            }

            newOrder.set(FIX::OrdType(FIX::OrdType_LIMIT));
            newOrder.set(FIX::TimeInForce(FIX::TimeInForce_DAY));
            newOrder.set(FIX::OrderQty(order.quantity));
            newOrder.set(FIX::Price(order.price));
            newOrder.set(FIX::TransactTime(FIX::UtcTimeStamp::now()));
            
            FIX::Session::sendToTarget(newOrder, currentSession);
            
            std::cout << "Sent new order: " << clOrdID << " for " << order.symbol
                << " (" << order.side << ", " << order.quantity << " @ " << order.price << ")" << std::endl;
    } catch (std::exception& e) {
        std::cerr << "Error sending order: " << e.what() << std::endl;
    }
    }
};

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cout << "Usage: " << argv[0] << " <config_file>" << std::endl;
        return 1;
    }
    std::string configFile = argv[1];
    try {
        FIX::SessionSettings settings(configFile);
        FIXApplication application;
        FIX::FileStoreFactory storeFactory(settings);
        FIX::FileLogFactory logFactory(settings);
        FIX::SocketInitiator initiator(application, storeFactory, settings, logFactory);
        std::cout << "Starting initiator..." << std::endl;
        initiator.start();

        //start http server
        application.startServer(8080);

        std::cout << "FIX application is running. Press Enter to quit" << std::endl;
        char input;
        std::cin.get(input);

        std::cout << "Stopping application..." << std::endl;
        application.stopServer();
        initiator.stop();

        return 0;

    } catch (std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
