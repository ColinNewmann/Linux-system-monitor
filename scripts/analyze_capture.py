"""重現此次 Ethernet／IPv4 TCP pcap 的分析，不需要第三方套件。"""
import collections
import datetime
import hashlib
import json
from pathlib import Path
import struct

folder = Path(__file__).resolve().parents[1] / 'docs/evidence/packet-capture'
path = folder / 'monitor.pcap'
data = path.read_bytes()
assert data[:4] == b'\xd4\xc3\xb2\xa1', 'expected little-endian microsecond classic pcap'
assert len(data) >= 24
_, major, minor, _, _, snaplen, linktype = struct.unpack('<IHHIIII', data[:24])
assert (major, minor, linktype) == (2, 4, 1), 'this analysis expects Ethernet pcap'
offset, packets = 24, []
while offset < len(data):
    assert offset + 16 <= len(data), 'truncated packet header'
    seconds, micros, saved, original = struct.unpack_from('<IIII', data, offset)
    offset += 16
    assert offset + saved <= len(data) and saved == original, 'truncated packet'
    frame = data[offset:offset + saved]
    offset += saved
    assert frame[12:14] == b'\x08\x00', 'expected IPv4 Ethernet frame'
    ip = frame[14:]
    assert ip[0] >> 4 == 4 and ip[9] == 6, 'expected IPv4 TCP'
    ip_size = (ip[0] & 15) * 4
    total = struct.unpack_from('!H', ip, 2)[0]
    tcp = ip[ip_size:total]
    source, destination, seq, ack = struct.unpack_from('!HHII', tcp)
    payload = tcp[(tcp[12] >> 4) * 4:]
    timestamp = datetime.datetime.fromtimestamp(seconds, datetime.timezone.utc)
    timestamp += datetime.timedelta(microseconds=micros)
    packets.append({'packet': len(packets) + 1, 'utc': timestamp.isoformat(timespec='microseconds'),
                    'time_us': seconds * 1000000 + micros,
                    'src': '.'.join(map(str, ip[12:16])), 'src_port': source,
                    'dst': '.'.join(map(str, ip[16:20])), 'dst_port': destination,
                    'seq': seq, 'ack': ack, 'flags': tcp[13],
                    'payload_bytes': len(payload), 'payload': payload.decode('ascii'),
                    'payload_hex': payload.hex()})

flows = collections.defaultdict(list)
for packet in packets:
    assert packet['src'] == packet['dst'] == '127.0.0.1'
    assert 8080 in (packet['src_port'], packet['dst_port'])
    client = packet['src_port'] if packet['dst_port'] == 8080 else packet['dst_port']
    flows[client].append(packet)

rows = []
expected = {b'GET_STATUS\n': 'OK STATUS ', b'GET_CPU\n': 'OK CPU ',
            b'GET_MEM\n': 'OK MEM ', b'GET_UPTIME\n': 'OK UPTIME '}
seen = set()
for client, flow in flows.items():
    # 此次每筆訊息都在單一 segment；不將此假設套用至一般 TCP。
    assert [p['flags'] for p in flow[:3]] == [2, 18, 16], 'handshake missing'
    assert [p['flags'] for p in flow[-3:]] == [17, 17, 16], 'normal FIN closure missing'
    assert all(not p['flags'] & 4 for p in flow), 'unexpected reset'
    syn, synack, ack = flow[:3]
    assert synack['ack'] == (syn['seq'] + 1) % 2**32
    assert ack['ack'] == (synack['seq'] + 1) % 2**32
    payloads = [p for p in flow if p['payload_bytes']]
    if not payloads:
        rows.append({'client_port': client, 'kind': 'readiness_probe', 'packets': len(flow)})
        continue
    assert len(payloads) == 2
    request, response = payloads
    raw_request = request['payload'].encode('ascii')
    assert raw_request in expected and raw_request not in seen
    seen.add(raw_request)
    assert request['dst_port'] == 8080 and response['src_port'] == 8080
    assert response['payload'].startswith(expected[raw_request])
    assert response['payload'].endswith('\n')
    assert response['ack'] == (request['seq'] + request['payload_bytes']) % 2**32
    rows.append({'client_port': client, 'kind': 'query', 'command': raw_request.decode().strip(),
                 'packets': len(flow), 'request_packet': request['packet'],
                 'response_packet': response['packet'], 'request_bytes': request['payload_bytes'],
                 'response_bytes': response['payload_bytes'], 'request_utc': request['utc'],
                 'response_utc': response['utc'],
                 'request_to_response_ms': (response['time_us'] - request['time_us']) / 1000,
                 'response': response['payload']})
assert seen == set(expected), 'not all four queries captured'
summary = {'pcap': path.name, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
           'link_type': 'EN10MB', 'snaplen': snaplen, 'packet_count': len(packets),
           'connections': len(flows), 'syn': sum(p['flags'] == 2 for p in packets),
           'syn_ack': sum(p['flags'] == 18 for p in packets),
           'fin': sum(bool(p['flags'] & 1) for p in packets),
           'rst': sum(bool(p['flags'] & 4) for p in packets),
           'payload_packets': sum(bool(p['payload_bytes']) for p in packets),
           'flows': rows, 'status': 'PASS'}
(folder / 'analysis.json').write_text(json.dumps(summary, indent=2) + '\n')
print(json.dumps(summary, indent=2))
