- client to do http request/response to epoch server
- mlspp cmake integration - done
- libquicr cmake integration - done
- code to use mls-api for setup mls-session (client.rs from client-nursery)
- code to integrate with libquicr

```
-------------------------------------------------------------------------
webex/com  |  meeting123 |    join-req         | bob       | 0   
0x0011223  |  445566     |    00 (req-type)    | 0001      | AABBCCDDEEFF
provider   |  conference |    join/leave/      | endpoint  |   CTR
-------------------------------------------------------------------------
```

  - subscribe to requests = Alice subscribes to 0x0011223344556600/56
  - publish intent to join = Bob sends KP on the name: 0x00112233445566000001000000000000
  - subscribe to self's welcome
  - publish welcome to the joiner
  - subscribe to commits
  - publish commits to everyone