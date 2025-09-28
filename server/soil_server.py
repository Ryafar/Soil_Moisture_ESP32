from http.server import HTTPServer, BaseHTTPRequestHandler
import json
import datetime
import csv
import os


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

                # Save to CSV
                self.save_to_csv(data, timestamp)

                self.send_response(200)
                self.send_header('Content-type', 'application/json')
                self.end_headers()
                self.wfile.write(b'{"status": "success"}')
            except Exception as e:
                print(f"Error processing data: {e}")
                self.send_response(400)
                self.end_headers()
        else:
            self.send_response(404)
            self.end_headers()

    def save_to_csv(self, data, timestamp):
        """Save data to CSV file"""
        filename = f"soil_data_{datetime.datetime.now().strftime('%Y%m%d')}.csv"
        file_exists = os.path.isfile(filename)

        with open(filename, 'a', newline='') as csvfile:
            fieldnames = ['timestamp', 'device_id',
                          'moisture_percent', 'voltage', 'raw_adc']
            writer = csv.DictWriter(csvfile, fieldnames=fieldnames)

            # Write header if file is new
            if not file_exists:
                writer.writeheader()

            # Write data
            writer.writerow({
                'timestamp': timestamp.isoformat(),
                'device_id': data['device_id'],
                'moisture_percent': data['moisture_percent'],
                'voltage': data['voltage'],
                'raw_adc': data['raw_adc']
            })


if __name__ == '__main__':
    server = HTTPServer(('0.0.0.0', 8080), SoilDataHandler)
    print("Server running on http://0.0.0.0:8080/soil-data")
    print("Data will be saved to daily CSV files: soil_data_YYYYMMDD.csv")
    server.serve_forever()
