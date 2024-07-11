# Media over Quic (MOQ) Transport API

### MOQ Instance
Instance is the connection handler for the client connection and server listening socket. 

 Class                          | Description                                                      
--------------------------------|------------------------------------------------------------------
quicr::MoQInstance             | MOQ Instance connection supporting server/relay and client modes 
quicr::MoQInstanceClientConfig | MOQ Instance configuration using client mode            
quicr::MoQInstanceServerConfig | MOQ Instance configuration using server mode
quicr::MoQInstanceDelegate | MOQ Instance delegate for callbacks on control and connection related events

### Publish and Subscribe Track Delegate

quicr::MoQTrackDelegate track delegate is used for subscribe, publish, or both. It implements both
receive and sending operations.



