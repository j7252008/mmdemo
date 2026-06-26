#include "ITransport.h"

#include "AsioClientTransport.h"
#include "AsioServerTransport.h"

namespace mm {
namespace net {

std::unique_ptr<IServerTransport> create_server_transport(TransportBackend backend)
{
    switch (backend) {
        case TransportBackend::Asio:
            return std::make_unique<AsioServerTransport>();
    }
    return nullptr;
}

std::unique_ptr<IClientTransport> create_client_transport(TransportBackend backend)
{
    switch (backend) {
        case TransportBackend::Asio:
            return std::make_unique<AsioClientTransport>();
    }
    return nullptr;
}

}  // namespace net
}  // namespace mm
