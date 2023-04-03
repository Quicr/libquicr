# QuicR Unsubscribe flows

> **1)** Client initiates ```unsubscribe()```

1. Client runs ```QuicRClient::unsubscribe()```
2. QuicRClient API sends QuicR **unsubscribe** message to server
3. packet is received by QuicR server
4. QuicR **server** API runs ```ServerDelegate::onUnsubscribe()``` to notify the server application
5. Server application (e.g., LAPS) implements ```ServerDelegate::onUnsubscribe()```  and does the following:
  * Frees any state for tracking the subscription
  * Runs ```ServerDelegate::subscriptionEnded()```
5. QuicR **server** API frees internal state of the subscription
6. QuicR server API sends **subscriptionEnded** message to client
7. packet is received by QuicR **client**
8. QuicR **client** API does the following:
  * runs ```SubscriberDelegate::onSubscriptionEnded()``` to notify the client APP
  * Frees any internal state, includes removing the subscriber delegate. **The client provided delegate will no longer be called at this point.**
9. Client implements ```SubscriberDelegate::onSubscriptionEnded()``` and closes out any state it has, including if it should free/delete the subscriber delegate


> **2)** Server APP initiates the unsubscribe

1. Server application runs ```QuicRServer::subscriptionEnded()```
2. QuicR server API sends **subscriptionEnded** message to client
3. Server application frees any subscription resources
4. packet is received by QuicR **client**
5. QuicR **client** API does the following:
  * runs ```SubscriberDelegate::onSubscriptionEnded()``` to notify the client
  * Frees any internal state, includes removing the subscriber delegate. **The client provided delegate will no longer be called at this point.**
6. Client implements ```SubscriberDelegate::onSubscriptionEnded()``` and closes out any state it has, including if it should free/delete the subscriber delegate

> **3)** QuicR Server API initiates the unsubscribe

This is the same flow as **(2)** with the below step added first.

0. QuicR Server API runs ```ServerDelegate::onUnsubscribe()```. This triggers the same flow as **(2)**


> **4)** QuicR Client API initiates the unsubscribe

Currently, the client API only initiates an unsubscribe locally. It will not send anything to the server side because the use-case for client API to close a subscription is when the connection to a server has been lost.  There might be other use-cases added in the future as we come across them. 

1. QuicR **client** API does the following:
  * runs ```SubscriberDelegate::onSubscriptionEnded()``` to notify the client
  * Frees any internal state, includes removing the subscriber delegate. **The client provided delegate will no longer be called at this point.**
2. Client implements ```SubscriberDelegate::onSubscriptionEnded()``` and closes out any state it has, including if it should free/delete the subscriber delegate

