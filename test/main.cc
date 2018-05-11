#include "stdafx.h"

#include "ipc.hh"

#include <cstdio>
#include <cstdlib>
#include <thread>

void on_packet(const char *prefix, ipc::Packet *p) {
	char *string_out = nullptr;
	int int_out = 0;
	float float_out = 0.0f;

	ipc::deserialize_packet(p, &int_out, &float_out, &string_out);

	printf("%s recieved %d: %s %d %f\n", prefix, p->id, string_out, int_out, float_out);
}

void server_on_packet(ipc::Packet *p) {
	on_packet("server", p);
}

void client_on_packet(ipc::Packet *p) {
	on_packet("client", p);
}


auto create_random_packet() {
	std::string s;

	int max = rand() % 50;
	for (int i = 0; i < max; i++) {
		s += 'A' + rand() % 25;
	}

	u32 int_in = rand();
	float float_in = (1.0 / rand()) * RAND_MAX;

	return ipc::create_packet(1, int_in, float_in, s.c_str());
}

bool finish_test = false;

void server_thread() {
	ipc::Server *server;
	server = new ipc::Server("ipc_test", 0x10000, 0x10000, &server_on_packet);

	while (!finish_test) {
		for (int i = 0; i < 100; i++) {
			server->send_packet_to(create_random_packet(), 1);
		}
		server->process_incoming();
	
		std::this_thread::yield();
		using namespace std::chrono_literals;
		std::this_thread::sleep_for(10ms);
	}

	delete server;
}

void client_thread() {
	ipc::Client *client;
	client = new ipc::Client("ipc_test", 0x10000, 0x10000, &client_on_packet);

	while(!finish_test) {
		for (int i = 0; i < 10; i++) {
			client->send_packet_to(create_random_packet(), 0);
		}
		client->process_incoming();

		std::this_thread::yield();
		using namespace std::chrono_literals;
		std::this_thread::sleep_for(10ms);
	}

	delete client;

}


int main() {
	std::thread st{ &server_thread };

	Sleep(100);

	std::thread ct{ &client_thread };

	meme();
	system("pause");

	finish_test = true;
	ct.join();
	st.join();
}
