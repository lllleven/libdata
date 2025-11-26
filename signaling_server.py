#!/usr/bin/env python3
"""
libdatachannel 信令服务器
用于在发送端和接收端之间交换WebRTC信令信息（SDP和ICE候选者）

使用方法:
    python3 signaling_server.py [端口]

示例:
    python3 signaling_server.py 9227

默认端口: 9227
"""

from flask import Flask, request, jsonify
from flask_cors import CORS
import threading
import time
from collections import defaultdict
from urllib.parse import unquote

app = Flask(__name__)
CORS(app)  # 允许跨域请求

# 存储信令数据
# 结构: {session_id: {'offer': sdp, 'answer': sdp, 'sender_candidates': [...], 'receiver_candidates': [...]}}
signaling_data = defaultdict(lambda: {
    'offer': None,
    'answer': None,
    'sender_candidates': [],
    'receiver_candidates': []
})

# 线程锁
locks = defaultdict(threading.Lock)

# 清理过期会话（超过5分钟）
def cleanup_old_sessions():
    while True:
        time.sleep(60)  # 每分钟检查一次
        current_time = time.time()
        # 这里可以添加会话超时清理逻辑

cleanup_thread = threading.Thread(target=cleanup_old_sessions, daemon=True)
cleanup_thread.start()

@app.route('/health', methods=['GET'])
def health():
    """健康检查"""
    return jsonify({'status': 'ok'})

@app.route('/session/<session_id>/offer', methods=['GET', 'POST'])
def set_offer(session_id):
    """发送端设置Offer（支持GET和POST）"""
    sdp = None
    if request.method == 'GET':
        sdp = request.args.get('sdp')
        if sdp:
            sdp = unquote(sdp)
    else:
        data = request.json
        if data and 'sdp' in data:
            sdp = data['sdp']
    
    if not sdp:
        return jsonify({'error': 'Missing sdp field'}), 400
    
    with locks[session_id]:
        signaling_data[session_id]['offer'] = sdp
        signaling_data[session_id]['sender_candidates'] = []
        signaling_data[session_id]['receiver_candidates'] = []
    
    print(f"[信令] Session {session_id}: Offer已设置")
    return jsonify({'status': 'ok'})

@app.route('/session/<session_id>/offer', methods=['GET'])
def get_offer(session_id):
    """接收端获取Offer"""
    with locks[session_id]:
        offer = signaling_data[session_id]['offer']
    
    if offer is None:
        return jsonify({'error': 'Offer not found'}), 404
    
    print(f"[信令] Session {session_id}: Offer已获取")
    return jsonify({'sdp': offer})

@app.route('/session/<session_id>/answer', methods=['GET', 'POST'])
def set_answer(session_id):
    """接收端设置Answer（支持GET和POST）"""
    sdp = None
    if request.method == 'GET':
        sdp = request.args.get('sdp')
        if sdp:
            sdp = unquote(sdp)
    else:
        data = request.json
        if data and 'sdp' in data:
            sdp = data['sdp']
    
    if not sdp:
        return jsonify({'error': 'Missing sdp field'}), 400
    
    with locks[session_id]:
        signaling_data[session_id]['answer'] = sdp
    
    print(f"[信令] Session {session_id}: Answer已设置")
    return jsonify({'status': 'ok'})

@app.route('/session/<session_id>/answer', methods=['GET'])
def get_answer(session_id):
    """发送端获取Answer"""
    with locks[session_id]:
        answer = signaling_data[session_id]['answer']
    
    if answer is None:
        return jsonify({'error': 'Answer not found'}), 404
    
    print(f"[信令] Session {session_id}: Answer已获取")
    return jsonify({'sdp': answer})

@app.route('/session/<session_id>/candidate/sender', methods=['GET', 'POST'])
def add_sender_candidate(session_id):
    """发送端添加候选者（支持GET和POST）"""
    candidate = None
    mid = None
    
    if request.method == 'GET':
        candidate = request.args.get('candidate')
        mid = request.args.get('mid')
        if candidate:
            candidate = unquote(candidate)
        if mid:
            mid = unquote(mid)
    else:
        data = request.json
        if data:
            candidate = data.get('candidate')
            mid = data.get('mid')
    
    if not candidate or not mid:
        return jsonify({'error': 'Missing candidate or mid field'}), 400
    
    with locks[session_id]:
        signaling_data[session_id]['sender_candidates'].append({
            'candidate': candidate,
            'mid': mid
        })
    
    return jsonify({'status': 'ok'})

@app.route('/session/<session_id>/candidate/sender', methods=['GET'])
def get_sender_candidates(session_id):
    """接收端获取发送端的候选者"""
    with locks[session_id]:
        candidates = signaling_data[session_id]['sender_candidates'].copy()
    
    return jsonify({'candidates': candidates})

@app.route('/session/<session_id>/candidate/receiver', methods=['GET', 'POST'])
def add_receiver_candidate(session_id):
    """接收端添加候选者（支持GET和POST）"""
    candidate = None
    mid = None
    
    if request.method == 'GET':
        candidate = request.args.get('candidate')
        mid = request.args.get('mid')
        if candidate:
            candidate = unquote(candidate)
        if mid:
            mid = unquote(mid)
    else:
        data = request.json
        if data:
            candidate = data.get('candidate')
            mid = data.get('mid')
    
    if not candidate or not mid:
        return jsonify({'error': 'Missing candidate or mid field'}), 400
    
    with locks[session_id]:
        signaling_data[session_id]['receiver_candidates'].append({
            'candidate': candidate,
            'mid': mid
        })
    
    return jsonify({'status': 'ok'})

@app.route('/session/<session_id>/candidate/receiver', methods=['GET'])
def get_receiver_candidates(session_id):
    """发送端获取接收端的候选者"""
    with locks[session_id]:
        candidates = signaling_data[session_id]['receiver_candidates'].copy()
    
    return jsonify({'candidates': candidates})

@app.route('/session/<session_id>', methods=['DELETE'])
def clear_session(session_id):
    """清理会话数据"""
    with locks[session_id]:
        if session_id in signaling_data:
            del signaling_data[session_id]
    
    print(f"[信令] Session {session_id}: 已清理")
    return jsonify({'status': 'ok'})

if __name__ == '__main__':
    import sys
    
    port = 9227
    if len(sys.argv) > 1:
        port = int(sys.argv[1])
    
    print(f"========================================")
    print(f"libdatachannel 信令服务器")
    print(f"========================================")
    print(f"监听端口: {port}")
    print(f"========================================")
    print(f"API端点 (所有端点都支持GET方法，通过查询参数传递数据):")
    print(f"  GET    /session/<id>/offer?sdp=...   - 设置/获取Offer")
    print(f"  GET    /session/<id>/answer?sdp=... - 设置/获取Answer")
    print(f"  GET    /session/<id>/candidate/sender?candidate=...&mid=... - 添加/获取发送端候选者")
    print(f"  GET    /session/<id>/candidate/receiver?candidate=...&mid=... - 添加/获取接收端候选者")
    print(f"  DELETE /session/<id>                 - 清理会话")
    print(f"========================================")
    
    app.run(host='0.0.0.0', port=port, threaded=True)

