from http.server import HTTPServer, BaseHTTPRequestHandler
import json
import datetime
import csv
import os

DATA_DIRECTORY = "./server/soil_data_logs/"

class SoilDataHandler(BaseHTTPRequestHandler):

    def log_message(self, format, *args):
        """Override to ensure HTTP access logs are displayed"""
        # print(f"{self.address_string()} - - [{self.log_date_time_string()}] {format % args}")
        pass

    def do_POST(self):
        if self.path == '/soil-data':
            content_length = int(self.headers['Content-Length'])
            post_data = self.rfile.read(content_length)

            try:
                data = json.loads(post_data.decode('utf-8'))

                timestamp = datetime.datetime.fromtimestamp(
                    data['timestamp'] / 1000)
                
                # Handle different data types
                data_type = data.get('type', 'soil')  # Default to 'soil' for backward compatibility
                
                if data_type == 'soil':
                    print(f"[{timestamp}] SOIL - device_id={data['device_id']}\tMoisture={data['moisture_percent']:.1f}%\tVoltage={data['voltage']:.3f}V\tRaw ADC={data['raw_adc']}")
                elif data_type == 'battery':
                    print(f"[{timestamp}] BATTERY - device_id={data['device_id']}\tVoltage={data['voltage']:.3f}V")
                else:
                    print(f"[{timestamp}] UNKNOWN TYPE({data_type}) - device_id={data['device_id']}\tVoltage={data['voltage']:.3f}V")

                # Save to CSV
                self.save_to_csv(data, timestamp, data_type)

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

    def save_to_csv(self, data, timestamp, data_type='soil'):
        """Save data to CSV file based on data type"""
        if not os.path.exists(DATA_DIRECTORY):
            os.makedirs(DATA_DIRECTORY)

        # Create separate CSV files for different data types
        if data_type == 'battery':
            filename = f"{DATA_DIRECTORY}/battery_data_{datetime.datetime.now().strftime('%Y%m%d')}.csv"
            fieldnames = ['timestamp', 'device_id', 'voltage', 'data_type']
            row_data = {
                'timestamp': timestamp.isoformat(),
                'device_id': data['device_id'],
                'voltage': data['voltage'],
                'data_type': data_type
            }
        else:  # soil or unknown types
            filename = f"{DATA_DIRECTORY}/soil_data_{datetime.datetime.now().strftime('%Y%m%d')}.csv"
            fieldnames = ['timestamp', 'device_id', 'moisture_percent', 'voltage', 'raw_adc', 'data_type']
            row_data = {
                'timestamp': timestamp.isoformat(),
                'device_id': data['device_id'],
                'moisture_percent': data.get('moisture_percent', 0),
                'voltage': data['voltage'],
                'raw_adc': data.get('raw_adc', 0),
                'data_type': data_type
            }

        file_exists = os.path.isfile(filename)
        
        with open(filename, 'a', newline='') as csvfile:
            writer = csv.DictWriter(csvfile, fieldnames=fieldnames)

            # Write header if file is new
            if not file_exists:
                writer.writeheader()

            # Write data
            writer.writerow(row_data)


if __name__ == '__main__':
    server = HTTPServer(('0.0.0.0', 8080), SoilDataHandler)
    print("Server running on http://0.0.0.0:8080/soil-data")
    print("Data will be saved to daily CSV files: soil_data_YYYYMMDD.csv")
    server.serve_forever()
