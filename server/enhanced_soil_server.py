from http.server import HTTPServer, BaseHTTPRequestHandler
import ssl
import json
import datetime
import csv
import os
from pathlib import Path


class SoilDataHandler(BaseHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        # Create data directory if it doesn't exist
        self.data_dir = Path("soil_data")
        self.data_dir.mkdir(exist_ok=True)
        super().__init__(*args, **kwargs)
    
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

                # Print to console
                print(f"[{timestamp}] Device: {data['device_id']}")
                print(f"  Moisture: {data['moisture_percent']:.1f}%")
                print(f"  Voltage: {data['voltage']:.3f}V")
                print(f"  Raw ADC: {data['raw_adc']}")
                print("-" * 50)

                # Save to CSV file
                self.save_to_csv(data, timestamp)

                # Send success response
                self.send_response(200)
                self.send_header('Content-type', 'application/json')
                self.send_header('Access-Control-Allow-Origin', '*')
                self.end_headers()
                response = {
                    "status": "success",
                    "message": "Data received and saved",
                    "timestamp": timestamp.isoformat()
                }
                self.wfile.write(json.dumps(response).encode())

            except Exception as e:
                print(f"Error processing data: {e}")
                self.send_response(400)
                self.send_header('Content-type', 'application/json')
                self.end_headers()
                error_response = {"status": "error", "message": str(e)}
                self.wfile.write(json.dumps(error_response).encode())

        elif self.path == '/test':
            # Test endpoint
            self.send_response(200)
            self.send_header('Content-type', 'application/json')
            self.end_headers()
            response = {"status": "server_running",
                        "message": "Soil sensor server is active"}
            self.wfile.write(json.dumps(response).encode())

        else:
            self.send_response(404)
            self.send_header('Content-type', 'application/json')
            self.end_headers()
            self.wfile.write(
                b'{"status": "error", "message": "Endpoint not found"}')

    def do_GET(self):
        if self.path == '/status':
            self.send_response(200)
            self.send_header('Content-type', 'application/json')
            self.end_headers()
            response = {
                "status": "running",
                "server": "Soil Moisture Data Server",
                "time": datetime.datetime.now().isoformat(),
                "endpoints": ["/soil-data (POST)", "/status (GET)", "/test (POST)"]
            }
            self.wfile.write(json.dumps(response, indent=2).encode())
        else:
            self.send_response(404)
            self.end_headers()

    def save_to_csv(self, data, timestamp):
        """Save data to CSV file"""
        csv_file = self.data_dir / f"soil_data_{datetime.date.today()}.csv"

        # Check if file exists to write headers
        file_exists = csv_file.exists()

        with open(csv_file, 'a', newline='') as f:
            writer = csv.writer(f)
            if not file_exists:
                # Write headers
                writer.writerow(['timestamp', 'device_id',
                                'moisture_percent', 'voltage', 'raw_adc'])

            # Write data
            writer.writerow([
                timestamp.isoformat(),
                data['device_id'],
                data['moisture_percent'],
                data['voltage'],
                data['raw_adc']
            ])


def create_self_signed_cert():
    """Create a self-signed certificate for HTTPS"""
    try:
        import ssl
        import ipaddress
        from cryptography import x509
        from cryptography.x509.oid import NameOID
        from cryptography.hazmat.primitives import hashes
        from cryptography.hazmat.primitives.asymmetric import rsa
        from cryptography.hazmat.primitives import serialization
        import datetime

        # Generate private key
        private_key = rsa.generate_private_key(
            public_exponent=65537,
            key_size=2048,
        )

        # Create certificate
        subject = issuer = x509.Name([
            x509.NameAttribute(NameOID.COMMON_NAME, u"localhost"),
        ])

        cert = x509.CertificateBuilder().subject_name(
            subject
        ).issuer_name(
            issuer
        ).public_key(
            private_key.public_key()
        ).serial_number(
            x509.random_serial_number()
        ).not_valid_before(
            datetime.datetime.utcnow()
        ).not_valid_after(
            datetime.datetime.utcnow() + datetime.timedelta(days=365)
        ).add_extension(
            x509.SubjectAlternativeName([
                x509.DNSName(u"localhost"),
                x509.IPAddress(ipaddress.IPv4Address(u"127.0.0.1")),
            ]),
            critical=False,
        ).sign(private_key, hashes.SHA256())

        # Write certificate and key to files
        with open("cert.pem", "wb") as f:
            f.write(cert.public_bytes(serialization.Encoding.PEM))

        with open("key.pem", "wb") as f:
            f.write(private_key.private_bytes(
                encoding=serialization.Encoding.PEM,
                format=serialization.PrivateFormat.PKCS8,
                encryption_algorithm=serialization.NoEncryption()
            ))

        return True
    except ImportError:
        print("cryptography package not installed. Install with: pip install cryptography")
        return False
    except Exception as e:
        print(f"Error creating certificate: {e}")
        return False


if __name__ == '__main__':
    print("=== Soil Moisture Data Server ===")
    print(f"Starting server at {datetime.datetime.now()}")

    # Ask user for HTTP or HTTPS
    use_https = input("Use HTTPS? (y/n, default=n): ").lower().startswith('y')

    port = 8080
    if use_https:
        port = 8443
        # Check if certificates exist
        if not (os.path.exists("cert.pem") and os.path.exists("key.pem")):
            print("Creating self-signed certificate...")
            if not create_self_signed_cert():
                print("Failed to create certificate. Falling back to HTTP.")
                use_https = False

    server = HTTPServer(('0.0.0.0', port), SoilDataHandler)

    if use_https:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain('cert.pem', 'key.pem')
        server.socket = context.wrap_socket(server.socket, server_side=True)
        print(f"HTTPS Server running on https://0.0.0.0:{port}")
        print(
            f"ESP32 should connect to: https://192.168.1.13:{port}/soil-data")
        print("Note: ESP32 will need to ignore SSL certificate verification for self-signed cert")
    else:
        print(f"HTTP Server running on http://0.0.0.0:{port}")
        print(f"ESP32 should connect to: http://192.168.1.13:{port}/soil-data")

    print("\nEndpoints available:")
    print("  POST /soil-data - Receive sensor data")
    print("  GET  /status    - Server status")
    print("  POST /test      - Test connectivity")
    print("\nData will be saved to ./soil_data/ directory")
    print("Press Ctrl+C to stop the server\n")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n\nServer stopped by user")
        server.server_close()
