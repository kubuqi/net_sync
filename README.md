# net_sync
synchronize actions over network, without using NTP.


## Protocol
A UDP based message exchange protocl is designed.  A message will include 1 sequence number, 4 timestamps, and 1 time duration.

### Time Stamps.
Client will periodicaly send a ping message, with local time stampe t1 included.  The message looks like (T1);
On the server side, upon message receiving, it will set another timestamp into the same message. The message now looks like (T1, T2);
On the server side, right before the pong reply message send, it set the third timestamp. The message looks like (T1, T2, T3).
On the client side, upon receiving the reply, it set the forth timestamp. The message looks like (T1, T2, T3, T4).

Now with these four timestamp available, we can determin the network latency to be:
round_trip_network_delay = (T4-T1) - (T3-T2);

### Sequence Number
Sequence number is used to avoid confusion caused by out-of-order packets or a large network delay.

Every request sent by a client should have a unique ID, and server will reply this request with a value of ID+1;

### Time to Fire
This value will be set by server and return to client, to indicate a duration between the time server send the packet, and the planned next event on the server side.

Upon receiving the response, the client can roughly calculate the time of next server event with following formular:
time_to_fire = now - one_way_network_delay + msg.time_to_fire;

## Server
There are two threads, one simply sleep and fire the events, the other respond to UDP requests.


## Client
Only one thread at moment. It send request to server, wait for response, and calculate the time of server event, do the output, and then sleep.
