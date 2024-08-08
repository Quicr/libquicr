# Media over Quic Transport (MOQT) API

## MOQT Core
The MOQT protocol and API implementation is in quicr::MOQTCore. Both quicr::MOQTClient and quicr::MOQTServer
implement quicr::MOQTCore. 

### Client

 Class                      | Description                                                       
----------------------------|-------------------------------------------------------------------
 quicr::MOQTClient          | Client handler, which is specific to a QUIC IP connection         
 quicr::MOQTClientConfig    | Client configuration                                              

### Server

 Class                      | Description                                                            
----------------------------|------------------------------------------------------------------------
 quicr::MOQTServer          | Server handler, which is specific to the QUIC IP listening IP and port 
 quicr::MOQTServerConfig    | Server configuration                                                   

### Track Handlers

`publishTrack()` and `subscribeTrack()` use the below implemented handler classes. 

 Class                            | Description                                                            
----------------------------------|------------------------------------------------------------------------
 quicr::MOQTSubscribeTrackHandler | Subscribe track handler for subscribe related operations and callbacks 
 quicr::MOQTPublishTrackHandler   | Publish track handler for publish related operations and callbacks     

---

## Documentation Links

* [API Process Flows](../moqt-api-process-flows.html)
* [MOQT Implementation Details](https://github.com/Quicr/libquicr/blob/main/docs/moq-implementation.md)
* Quick Start
