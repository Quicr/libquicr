import argparse
import subprocess
import os
import signal
import sys
import string

parser = argparse.ArgumentParser("QPerf")
parser.add_argument("--clients", help="Number of client processes to spawn.", type=int, default=1)
parser.add_argument("--streams", help="Number of streams per client", type=int, default=1)
parser.add_argument("--chunk_size", help="Chunk size", type=int, default=3000)
parser.add_argument("--msg_size", help="Byte size of message", type=int, default=1024)
parser.add_argument("--relay_url", help="Relay port to connect on", type=str, default="relay.quicr.ctgpoc.com")
parser.add_argument("--relay_port", help="Relay port to connect on", type=int, default= 33435)
parser.add_argument("--duration", help="Duration of test in seconds.", type=int, default=120)
parser.add_argument("--interval", help="The interval in microseconds to send publish messages", type=int, default=1000)
parser.add_argument("--priority", help="Priority for sending publish messages", type=int, default=1)
parser.add_argument("--expiry_age", help="Expiry age of objects in ms", type=int, default=5000)
args = parser.parse_args()

print("+==========================================+")
print("| Starting test")
print("+------------------------------------------+")
print(f'| *         Duration: {args.duration} seconds')
print(f'| *            Relay: {args.relay_url}:{args.relay_port}')
print(f'| *          Clients: {args.clients}')
print(f'| *          Streams: {args.streams} per client')
print(f'| *          Bitrate: {(args.msg_size * 8) / (args.interval / 1e6)} bps')
print(f'| * Expected Objects: {1e6 / args.interval} per stream')
print(f'| *    Total Objects: {(1e6 / args.interval) * args.streams}')
print("+==========================================+")

cwd = os.getcwd()
qperf = cwd + "/qperf"
for i in range(args.clients):
    qperf_cmd =f'{qperf} -n 0x01020304050607080{i:x}10111213141516/80 --endpoint_id perf{i:x}@cisco.com -d {args.duration} --streams {args.streams} --chunk_size {args.chunk_size} --msg_size {args.msg_size} --relay_url {args.relay_url} --relay_port {args.relay_port} --interval {args.interval} --priority {args.priority} --expiry_age {args.expiry_age}'
    subprocess.Popen(qperf_cmd, shell=True, stderr=subprocess.DEVNULL)

def signal_handler(sig, frame):
    os.system("killall -SIGINT qperf")
    print("+==========================================+")
    print("| Test cancelled")
    print("+==========================================+")
    sys.exit(0)

signal.signal(signal.SIGINT, signal_handler)
print('Press Ctrl+C to cancel test')
signal.pause()

print("+==========================================+")
print("| Test Complete")
print("+==========================================+")
