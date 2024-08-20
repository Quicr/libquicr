# Media over QUIC Transport (MoQT) Publisher/Subscriber API

This library provides a publisher and subscriber API using MoQT. It provides
a class for [Client](#moq::transport::Client) and [Server](#moq::transport::Server) that
will handle all MoQT protocol state machine functions. Both classes contain pure virtual methods (e.g., callbacks)
that **MUST** be implemented. 

Subscriptions and publications are handled via [Subscribe Track Handler](#moq::transport::SubscribeTrackHandler) 
and [Publish Track Handler](#moq::transport::PublishTrackHandler). Both classes contain pure virtual methods
(e.g., callbacks) that **MUST** be implemented.

## Client

 Class                        | Description                                               
------------------------------|-----------------------------------------------------------
 moq::transport::Client       | Client handler, which is specific to a QUIC IP connection 
 moq::transport::ClientConfig | Client configuration                                      

## Server

 Class                        | Description                                                            
------------------------------|------------------------------------------------------------------------
 moq::transport::Server       | Server handler, which is specific to the QUIC IP listening IP and port 
 moq::transport::ServerConfig | Server configuration                                                   

## Track Handlers

Both client and server moq::transport::Transport::PublishTrack() and moq::transport::Transport::SubscribeTrack()
methods to start a new subscription and/or publication. Use the below handler classes when calling
the methods. Each track handler is constructed for a single full track name (e.g., namespace and name).

 Class                                 | Description                                                            
---------------------------------------|------------------------------------------------------------------------
 moq::transport::SubscribeTrackHandler | Subscribe track handler for subscribe related operations and callbacks 
 moq::transport::PublishTrackHandler   | Publish track handler for publish related operations and callbacks     

## Client Flow
<img src="../images/moqt-client-api.png" alt="Client Process Flow" style="height: auto; width:80%"/>

## Server Flow

## Documentation Links

* [API Process Flows](../docs/images/moqt-api-process-flows.html)
* [MOQT Implementation Details](https://github.com/Quicr/libquicr/blob/main/docs/moq-implementation.md)
* Quick Start
