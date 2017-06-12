# net_sync
Synchronize actions over network, without using NTP.


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
There are two threads, one ticking thread simply sleep and fire the output, the other (main thread) listen and respond to UDP requests.


## Client
Similar to the design of server, two threads coorperate to get the job done. The ticking thread schedule itself and fire the output, and the main thread will be responsible for talking to server and try calculate out the time when server started. Obviously if the client can calculate the time point when the server starts to output, it can predict when will server do the next output, and do it from client side at that exact time.

## Testing
Testing was done by running the client and server on same host, and setup the communication through loopback interface. The network latency was emulated with netem as suggested.

To benchmark the accuracy, server will dump the timestamp it started to a text file, and client will read in this timestamp, and compare it with the calculated one based on the result from UDP request. Because both programs were running on same host, their clock would be the same, and the difference would reveal the error from the algorythm of the choice.

## Futher enhancements
Currently the client simply adjust its epoch timestamp (the time when server started its first output) with the calculated result from each response, and it is deem to be influnced by the network jitter. This can be improved by employing some sort of filters.  Moving average and kalman filter are the two that should be considered.







