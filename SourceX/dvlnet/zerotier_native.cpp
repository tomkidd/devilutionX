#include "dvlnet/zerotier_native.h"

#include <atomic>
#include <SDL.h>
#include <cstdlib>
#include <ZeroTier.h>

#include "all.h"
#include "../3rdParty/Storm/Source/storm.h"
#include "paths.h"

#include "dvlnet/zerotier_lwip.h"

namespace dvl {
namespace net {

static constexpr uint64_t zt_earth = 0x8056c2e21c000001;

static std::atomic_bool zt_network_ready(false);
static std::atomic_bool zt_started(false);

static void callback(struct zts_callback_msg *msg)
{
	//printf("callback %i\n", msg->eventCode);
	if(msg->eventCode == ZTS_EVENT_NODE_ONLINE) {
		SDL_Log("ZeroTier: ZTS_EVENT_NODE_ONLINE, nodeId=%llx\n", (unsigned long long)msg->node->address);
		zts_join(zt_earth);
	} else if(msg->eventCode == ZTS_EVENT_NODE_OFFLINE) {
		SDL_Log("ZeroTier: ZTS_EVENT_NODE_OFFLINE\n");
		zt_network_ready = false;
	} else if(msg->eventCode == ZTS_EVENT_NETWORK_READY_IP6) {
		SDL_Log("ZeroTier: ZTS_EVENT_NETWORK_READY_IP6, networkId=%llx\n", (unsigned long long)msg->network->nwid);
		zt_ip6setup();
		zt_network_ready = true;
	} else if(msg->eventCode == ZTS_EVENT_ADDR_ADDED_IP6) {
		print_ip6_addr(&(msg->addr->addr));
	}
}

bool zerotier_network_ready()
{
	return zt_network_ready;
}

void zerotier_network_stop()
{
	zts_stop();
}

void zerotier_network_start()
{
	if(zt_started)
		return;
	zts_start(GetPrefPath().c_str(), callback, 0);
	std::atexit(zerotier_network_stop);
}

}
}
