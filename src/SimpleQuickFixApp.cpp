#include <iostream>
#include <string>
#include <quickfix/FileStore.h>
#include <quickfix/FileLog.h>
#include <quickfix/SocketInitiator.h>
#include <quickfix/SessionSettings.h>
#include <quickfix/Application.h>
#include <quickfix/MessageCracker.h>

class SimpleApplication : public FIX::Application, public FIX::MessageCracker {
private: 
	bool connected; 

public: 
	SimpleApplication() : connected(false) {
		
		void onCreate(const FIX::SessionID&) override {}
		
		void onLogon(const FIX::SessionID& sessionID) override {
			std::cout << "Logon - " << sessionID.toString() << std::endl;
			connected = true;
		}

		void onLogout(const FIX::SessionID& sessionID) override {
			std::cout << "Logout - " << sessionID.toString() << std:: endl;
			connected = false;
		}

		void toAdmin(FIX::Message& message, const FIX::SessionID& sessionID) override {}

		void fromAdmin(const FIX::Message& message, const FIX::SessionID& sessionID) throw(FIX::FieldNotFound, FIX::IncorrectDataFormat, FIX::IncorrectTagValue, FIX::RejectLogon) override {}

		void toApp(FIX::Message& message, const FIX::SessionID& sessionID)
			throw (FIX::DoNotSend) override {}

		void fromApp(const FIX::Message& message, const FIX::SessionID& sessionID) 
			throw(FIX::FieldNotFound, FIX::IncorrectDataFormat, FIX::IncorrectTagValue, FIX::UnsupportedMessageType) override {
			crack(message, sessionID);
		}

		bool isConnected() const {
			return connected;
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

			SimpleApplication application;

			FIX::FileStoreFactory storeFactory(settings);
			FIX::FileLogFactory logFactory(settings);

			FIX::SocketInitiator initiator(application, storeFactory, settings, logFactory);

			std::cout << "Starting initiator..." << std::endl;
			initiator.start();

			std::cout << "Press Enter to quit" << std::endl;
			char input;
			std::cin.get(input);

			std::cout << "Stopping initiator..." << std::endl;
			initiator.stop();
			return 0;
		} catch (std::exception& e) {
			std::cerr << "Error: " << e.what() << std::endl;
			return 1;
		}
	}
