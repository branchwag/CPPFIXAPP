#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <quickfix/FileStore.h>
#include <quickfix/FileLog.h>
#include <quickfix/SocketInitiator.h>
#include <quickfix/SessionSettings.h>
#include <quickfix/Application.h>
#include <quickfix/MessageCracker.h>
#include <quickfix/fix44/NewOrderSingle.h>
#include <quickfix/fix44/ExecutionReport.h>

class FIXApplication : public FIX::Application, public FIX::MessageCracker {
private: 
    std::atomic<bool> connected; 
    std::atomic<bool> stopRequested;
    std::thread orderThread;
    int orderID;

public: 
    FIXApplication() : connected(false), stopRequested(false), orderID(0) {}

    ~FIXApplication() {
        stopSending();
    }
    
    void onCreate(const FIX::SessionID&) override {}
    
    void onLogon(const FIX::SessionID& sessionID) override {
        std::cout << "Logon - " << sessionID.toString() << std::endl;
        connected = true;

        //send orders
        startSending(sessionID);
    }
    
    void onLogout(const FIX::SessionID& sessionID) override {
        std::cout << "Logout - " << sessionID.toString() << std::endl;
        connected = false;
        stopSending();
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

    void startSending(const FIX::SessionID& sessionID) {
        stopSending();

        stopRequested = false;
        orderThread = std::thread([this, sessionID] () {
            while (!stopRequested) {
                if (connected) {
                        sendNewOrderSingle(sessionID);
                } 
                std::this_thread::sleep_for(std::chrono::milliseconds(60000));
            }
        });
    }

    void stopSending() {
        stopRequested = true;
        if (orderThread.joinable()) {
            orderThread.join();
        }
    }

    void sendNewOrderSingle(const FIX::SessionID& sessionID) {
        try {
            FIX44::NewOrderSingle newOrder;
            std::string clOrdID = "ORDER" + std::to_string(++orderID);
            
            newOrder.set(FIX::ClOrdID(clOrdID));
            newOrder.set(FIX::Symbol("ZVZZT"));
            newOrder.set(FIX::Side(FIX::Side_BUY));
            newOrder.set(FIX::OrdType(FIX::OrdType_LIMIT));
            newOrder.set(FIX::TimeInForce(FIX::TimeInForce_DAY));
            newOrder.set(FIX::OrderQty(100));
            newOrder.set(FIX::Price(25.00));
            newOrder.set(FIX::TransactTime(FIX::UTCTIMESTAMP()));
            
            FIX::Session::sendToTarget(newOrder, sessionID);
            
            std::cout << "Sent new order: " << clOrdID << " for ZVZZT" << std::endl;
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
        std::cout << "Press Enter to quit" << std::endl;
        char input;
        std::cin.get(input);
        std::cout << "Stopping initiator..." << std::endl;
        application.stopSending();
        initiator.stop();
        return 0;
    } catch (std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
