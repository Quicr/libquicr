# Media over Quic Transport (MOQT) API

## MOQ Implementation Instance
Instance is the connection handler for the client connection and server listening socket. 

### Client

 Class                     | Description                                                                    
---------------------------|--------------------------------------------------------------------------------
 quicr::MOQTClient         | Client handler, which is specific to a QUIC IP connection                      
 quicr::MOQTClientConfig   | Client configuration                                                           
 quicr::MOQTClientDelegate | Client delegate for callbacks (control messages and connection related events) 

### Server

 Class                     | Description                                                                    
---------------------------|--------------------------------------------------------------------------------
 quicr::MOQTServer         | Server handler, which is specific to the QUIC IP listening IP and port         
 quicr::MOQTServerConfig   | Server configuration                                                           
 quicr::MOQTServerDelegate | Server delegate for callbacks (control messages and connection related events) 

### Track Handlers

`publishTrack()` and `subscribeTrack()` use the below implemented handler classes. 

 Class                            | Description                                                            
----------------------------------|------------------------------------------------------------------------
 quicr::MOQTSubscribeTrackHandler | Subscribe track handler for subscribe related operations and callbacks 
 quicr::MOQTPublishTrackHandler   | Publish track handler for publish related operations and callbacks     

---

## Documentation Links

* [MoQ Implementation Details](https://github.com/Quicr/libquicr/blob/main/docs/moq-implementation.md)
* Quick Start
