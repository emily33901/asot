#include "stdafx.h"

#include "ipc_packet.hh"

#ifdef _MSC_VER
using Handle = HANDLE;
#else
using Handle = void *;
#endif

namespace ipc {

	class MutexLock {
		Handle m;

	public:
		MutexLock(Handle h) : m(h) { WaitForSingleObject(m, INFINITE); }

		~MutexLock() { ReleaseMutex(m); }
	};

	struct Header {
		Handle mutex;
		u32    last_client;

		// client --> server packets
		u32 incoming_size;

		// server --> client packets
		u32 outgoing_size;

		u32 last_id;
	};

	constexpr u32 header_size = sizeof(Header);

	enum class Type {
		server,
		client
	};

	using OnPacketFn = void(*)(Packet *);

	template <Type type>
	class Peer {
		// integral peer data
		Header *header;   // represents the entire buffer
		void *     incoming; // recieved
		void *     outgoing; // sent
		u32        total_size;

		Handle file_handle;
		bool   valid = false;

		// the id of this peer
		u32  peer_id;
		char obj_name[MAX_PATH];

		OnPacketFn callback;
	public:
		Peer(const char *object_name, u32 incoming_size, u32 outgoing_size, OnPacketFn callback);

		~Peer() {
			if (type == Type::client) {
			}

			if (valid) {
				UnmapViewOfFile(header);

				CloseHandle(file_handle);
			}
		}

		bool is_valid() { return valid; }

		void process_incoming() {
			assert(valid);

			{
				MutexLock m_lock{ header->mutex };

				u32 unprocessed_offset = 0;
				u8 *unprocessed = new u8[header->incoming_size];

				memset(unprocessed, 0, header->incoming_size);

				u32 offset = 0;
				while (true) {
					Packet *dat = (Packet *)((u32)incoming + offset);

					if (dat->extra_size == 0)
						break; // end of data

					u32 packet_size = sizeof(Packet) + dat->extra_size;
					offset += packet_size;

					if (peer_id == dat->to) {
						// TODO: pass data back up to be processed
						callback(dat);
					}
					else {
						// write to unprocessed buffer

						if (unprocessed_offset + packet_size > header->incoming_size) {
							// Unprocessed buffer has been overfloat
							assert(0);
							break;
						}

						memcpy(unprocessed + unprocessed_offset, dat, packet_size);
						unprocessed_offset += packet_size;
					}
				}

				// all incoming packets for us have been dealt with
				// copy unprocessed packets back into buffer
				memcpy(incoming, unprocessed, header->incoming_size);

				delete[] unprocessed;
			}
		}

		void send_packet_to(Packet *p, u32 to) {
			assert(valid);

			{
				p->from = peer_id;
				p->to = to;

				MutexLock m_lock{ header->mutex };

				header->last_id += 1;
				p->id = header->last_id;

				// get the last outgoing packet
				u32        offset = 0;
				Packet *dat = (Packet *)outgoing;
				while (true) {
					if (dat->extra_size == 0)
						break; // end of data

					offset += sizeof(Packet) + dat->extra_size;
					dat = (Packet *)((u32)outgoing + offset);
				}

				// if this goes off then you overflowed the buffer
				assert((offset + p->extra_size) < header->outgoing_size);

				// fill out packet
				memcpy(dat, p, sizeof(Packet) + p->extra_size);
			}
		}
	};

	template <>
	inline Peer<Type::server>::Peer(const char *object_name, u32 incoming_size, u32 outgoing_size, OnPacketFn callback)
		: total_size(incoming_size + outgoing_size + header_size), callback(callback) {
		sprintf(obj_name, "Local\\%s", object_name);

		file_handle = CreateFileMappingA(
			INVALID_HANDLE_VALUE, // use paging file
			nullptr,                 // default security
			PAGE_READWRITE,       // read/write access
			0,                    // maximum object size (high-order u32)
			total_size + 50,      // maximum object size (low-order u32)
			obj_name);            // name of mapping object

		if (file_handle == nullptr || file_handle == INVALID_HANDLE_VALUE) {
			return;
		}

		header = (Header *)MapViewOfFile(file_handle, FILE_MAP_ALL_ACCESS, 0, 0, total_size);

		if (header == nullptr) {
			CloseHandle(file_handle);
			return;
		}

		incoming = ((u8 *)header) + header_size;
		outgoing = ((u8 *)header) + header_size + incoming_size;

		peer_id = 0;

		// clean out buffers
		memset(incoming, 0, incoming_size);
		memset(outgoing, 0, outgoing_size);

		header->mutex = CreateMutexA(nullptr, FALSE, nullptr);
		header->last_client = 1;
		header->incoming_size = incoming_size;
		header->outgoing_size = outgoing_size;

		valid = true;
	}

	template <>
	inline Peer<Type::client>::Peer(const char *object_name, u32 incoming_size, u32 outgoing_size, OnPacketFn callback)
		: total_size(incoming_size + outgoing_size + header_size), callback(callback) {
		sprintf(obj_name, "Local\\%s", object_name);

		file_handle = OpenFileMappingA(
			FILE_MAP_ALL_ACCESS, // read/write access
			FALSE,               // do not inherit the name
			obj_name);           // name of mapping object

		if (file_handle == nullptr || file_handle == INVALID_HANDLE_VALUE) {
			return;
		}

		header = (Header *)MapViewOfFile(file_handle, FILE_MAP_ALL_ACCESS, 0, 0, total_size);

		if (header == nullptr) {
			CloseHandle(file_handle);
			return;
		}

		// in the client the incoming and outgoing buffers are flipped

		incoming = ((u8 *)header) + header_size + incoming_size;
		outgoing = ((u8 *)header) + header_size;

		{
			MutexLock m_lock{ header->mutex };

			peer_id = header->last_client;
			header->last_client += 1;
		}

		valid = true;
	}

	using Server = Peer<Type::server>;
	using Client = Peer<Type::client>;
} // namespace ipc
