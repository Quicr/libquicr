# Media over QUIC (MoQ) Publisher/Subscriber API

This library provides a publisher and subscriber API using MoQ. It provides
a class for [Client](#moq::Client) and [Server](#moq::Server) that
will handle all MoQ protocol state machine functions. Both classes contain pure virtual methods (e.g., callbacks)
that **MUST** be implemented. 

Subscriptions and publications are handled via [Subscribe Track Handler](#moq::SubscribeTrackHandler) 
and [Publish Track Handler](#moq::PublishTrackHandler). Both classes contain pure virtual methods
(e.g., callbacks) that **MUST** be implemented.

## Client

 Class                        | Description                                               
------------------------------|-----------------------------------------------------------
 moq::Client       | Client handler, which is specific to a QUIC IP connection 
 moq::ClientConfig | Client configuration                                      

## Server

 Class                        | Description                                                            
------------------------------|------------------------------------------------------------------------
 moq::Server       | Server handler, which is specific to the QUIC IP listening IP and port 
 moq::ServerConfig | Server configuration                                                   

## Track Handlers

Both client and server provide moq::PublishTrack() and moq::SubscribeTrack()
methods to start a new subscription and/or publication. Use the below handler classes when calling
the methods. Each track handler is constructed for a single full track name (e.g., namespace and name).

 Class                          | Description                                                            
--------------------------------|------------------------------------------------------------------------
 moq::SubscribeTrackHandler     | Subscribe track handler for subscribe related operations and callbacks 
 moq::PublishTrackHandler       | Publish track handler for publish related operations and callbacks     

## Client Flow
<img src="../images/MoQ-client-api.png" alt="Client Process Flow" style="height: auto; width:80%"/>

## Server Flow

## Documentation Links

* [API Process Flows](../MoQ-api-process-flows.html)
* [MoQ Implementation Details](https://github.com/Quicr/libquicr/blob/main/docs/moq-implementation.md)
* Quick Start
