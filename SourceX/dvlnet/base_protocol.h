#pragma once

#include <string>
#include <set>
#include <memory>

#include "dvlnet/packet.h"
#include "dvlnet/base.h"

namespace dvl {
namespace net {

template<class P>
class base_protocol : public base {
public:
	virtual int create(std::string addrstr, std::string passwd);
	virtual int join(std::string addrstr, std::string passwd);
	virtual void poll();
	virtual void send(packet &pkt);
	virtual void disconnect_net(plr_t plr);

	virtual bool SNetLeaveGame(int type);

	virtual std::string make_default_gamename();

	virtual ~base_protocol() = default;

private:
	P proto;
	typedef typename P::endpoint endpoint;

	endpoint firstpeer;
	std::string gamename;
	std::array<endpoint, MAX_PLRS> peers;

	plr_t get_master();
	void recv();
	void handle_join_request(packet &pkt, endpoint sender);
	void recv_decrypted(packet &pkt, endpoint sender);

	bool wait_network();
	bool wait_firstpeer();
	void wait_join();
};

template<class P>
plr_t base_protocol<P>::get_master()
{
	plr_t ret = plr_self;
	for (plr_t i = 0; i < MAX_PLRS; ++i)
		if(peers[i])
			ret = std::min(ret, i);
	return ret;
}

template<class P>
bool base_protocol<P>::wait_network()
{
	// wait for ZeroTier for 5 seconds
	for (auto i = 0; i < 500; ++i) {
		if (proto.network_online())
			break;
		SDL_Delay(10);
	}
	return proto.network_online();
}

template<class P>
void base_protocol<P>::disconnect_net(plr_t plr)
{
	proto.disconnect(peers[plr]);
	peers[plr] = endpoint();
}

template<class P>
bool base_protocol<P>::wait_firstpeer()
{
	// wait for peer for 5 seconds
	auto pkt = pktfty->make_packet<PT_INFO_REQUEST>(PLR_BROADCAST,
		                                                PLR_MASTER);
	for (auto i = 0; i < 500; ++i) {
		proto.send_oob_mc(pkt->data());
		recv();
		if (firstpeer)
			break; // got address
		SDL_Delay(10);
	}
	return (bool)firstpeer;
}

template<class P>
void base_protocol<P>::wait_join()
{
	randombytes_buf(reinterpret_cast<unsigned char *>(&cookie_self),
	                sizeof(cookie_t));
	auto pkt = pktfty->make_packet<PT_JOIN_REQUEST>(PLR_BROADCAST,
	                                                PLR_MASTER, cookie_self, game_init_info);
	proto.send(firstpeer, pkt->data());
	for (auto i = 0; i < 500; ++i) {
		recv();
		if (plr_self != PLR_BROADCAST)
			break; // join successful
		SDL_Delay(10);
	}
}

template<class P>
int base_protocol<P>::create(std::string addrstr, std::string passwd)
{
	setup_password(passwd);
	gamename = addrstr;

	if(wait_network()) {
		plr_self = 0;
		connected_table[plr_self] = true;
	}

	return (plr_self == PLR_BROADCAST ? MAX_PLRS : plr_self);
}

template<class P>
int base_protocol<P>::join(std::string addrstr, std::string passwd)
{
	//addrstr = "fd80:56c2:e21c:0:199:931d:b14:c4d2";
	setup_password(passwd);
	gamename = addrstr;
	if(wait_network())
		if(wait_firstpeer())
			wait_join();
	return (plr_self == PLR_BROADCAST ? MAX_PLRS : plr_self);
}

template<class P>
void base_protocol<P>::poll()
{
	recv();
}

template<class P>
void base_protocol<P>::send(packet &pkt)
{
	if(pkt.dest() < MAX_PLRS) {
		if(pkt.dest() == myplr)
			return;
		if(peers[pkt.dest()])
			proto.send(peers[pkt.dest()], pkt.data());
	} else if(pkt.dest() == PLR_BROADCAST) {
		for (auto &peer : peers)
			if(peer)
				proto.send(peer, pkt.data());
	} else if(pkt.dest() == PLR_MASTER) {
		throw dvlnet_exception();
	} else {
		throw dvlnet_exception();
	}
}

template<class P>
void base_protocol<P>::recv()
{
	try {
		buffer_t pkt_buf;
		endpoint sender;
		while (proto.recv(sender, pkt_buf)) { // read until kernel buffer is empty?
			try {
				auto pkt = pktfty->make_packet(pkt_buf);
				recv_decrypted(*pkt, sender);
			} catch (packet_exception &e) {
				// drop packet
				proto.disconnect(sender);
				SDL_Log(e.what());
			}
		}
	} catch (std::exception &e) {
		SDL_Log(e.what());
		return;
	}
}

template<class P>
void base_protocol<P>::handle_join_request(packet &pkt, endpoint sender)
{
	plr_t i;
	for (i = 0; i < MAX_PLRS; ++i) {
		if (i != plr_self && !peers[i]) {
			peers[i] = sender;
			break;
		}
	}
	if(i >= MAX_PLRS) {
		//already full
		return;
	}
	for (plr_t j = 0; j < MAX_PLRS; ++j) {
		if ((j != plr_self) && (j != i) && peers[j]) {
			auto infopkt = pktfty->make_packet<PT_CONNECT>(PLR_MASTER, PLR_BROADCAST, j, peers[j].serialize());
			proto.send(sender, infopkt->data());
			break;
		}
	}
	auto reply = pktfty->make_packet<PT_JOIN_ACCEPT>(plr_self, PLR_BROADCAST,
	    pkt.cookie(), i,
	    game_init_info);
	proto.send(sender, reply->data());
}

template<class P>
void base_protocol<P>::recv_decrypted(packet &pkt, endpoint sender)
{
	if (pkt.src() == PLR_BROADCAST && pkt.dest() == PLR_MASTER) {
		if(pkt.type() == PT_JOIN_REQUEST) {
			handle_join_request(pkt, sender);
		} else if(pkt.type() == PT_INFO_REQUEST) {
			//printf("GOT INFO REQUEST!\n");
			if((plr_self != PLR_BROADCAST) && (get_master() == plr_self)) {
				buffer_t buf;
				buf.resize(gamename.size());
				std::memcpy(buf.data(), &gamename[0], gamename.size());
				auto reply = pktfty->make_packet<PT_INFO_REPLY>(PLR_BROADCAST,
				                                                PLR_MASTER,
				                                                buf);
				proto.send_oob(sender, reply->data());
			}
		} else if(pkt.type() == PT_INFO_REPLY) {
			//printf("GOT INFO REPLY!\n");
			std::string pname;
			pname.resize(pkt.info().size());
			std::memcpy(&pname[0], pkt.info().data(), pkt.info().size());
			if(gamename == pname)
				firstpeer = sender;
		}
		return;
	} else if (pkt.src() == PLR_MASTER && pkt.type() == PT_CONNECT) {
		// addrinfo packets
		connected_table[pkt.newplr()] = true;
		peers[pkt.newplr()].unserialize(pkt.info());
		return;
	} else if (pkt.src() >= MAX_PLRS) {
		// normal packets
		ABORT();
	}
	connected_table[pkt.src()] = true;
	peers[pkt.src()] = sender;
	if (pkt.dest() != plr_self && pkt.dest() != PLR_BROADCAST)
		return; //packet not for us, drop
	recv_local(pkt);
}

template<class P>
bool base_protocol<P>::SNetLeaveGame(int type)
{
	auto ret = base::SNetLeaveGame(type);
	recv();
	return ret;
}

template<class P>
std::string base_protocol<P>::make_default_gamename()
{
	return proto.make_default_gamename();
}

} // namespace net
} // namespace dvl
