from http.server import HTTPServer, BaseHTTPRequestHandler
import json
import datetime


class SoilDataHandler(BaseHTTPRequestHandler):
    def do_POST(self):
        if self.path == '/soil-data':
            content_length = int(self.headers['Content-Length'])
            post_data = self.rfile.read(content_length)

            try:
                print(f"Received Soil Data: {post_data.decode('utf-8')}")
                data = json.loads(post_data.decode('utf-8'))
                print(f"Received Soil Data: {data}")

                timestamp = datetime.datetime.fromtimestamp(
                    data['timestamp'] / 1000)
                print(f"[{timestamp}] Device: {data['device_id']}")
                print(f"  Moisture: {data['moisture_percent']:.1f}%")
                print(f"  Voltage: {data['voltage']:.3f}V")
                print(f"  Raw ADC: {data['raw_adc']}")
                print("-" * 50)

                self.send_response(200)
                self.send_header('Content-type', 'application/json')
                self.end_headers()
                self.wfile.write(b'{"status": "success"}')
            except Exception as e:
                print(f"Error processing data: {e}")
                # TODO: Adjust server to give better response for invalid data packet format
                self.send_response(400)
                self.end_headers()
        else:
            self.send_response(404)
            self.end_headers()


if __name__ == '__main__':
    server = HTTPServer(('0.0.0.0', 8080), SoilDataHandler)
    print("Server running on http://0.0.0.0:8080/soil-data")
    server.serve_forever()
