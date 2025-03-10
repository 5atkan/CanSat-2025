import serial
import pyodbc
import time
import threading
import sys

# Global flag to know when Ctrl+D (EOF) is hit
restart_flag = False

# This thread waits for Ctrl+D and then sets the flag
def wait_for_ctrl_d():
    global restart_flag
    try:
        input()  # Just wait for user input
    except EOFError:
        restart_flag = True

def main():
    global restart_flag
    restart_flag = False

    # Basic serial and DB settings
    port = "COM6"  # Change if needed (Arduino with LoRa)
    baud = 9600

    db_server = "(local)\\SQLEXPRESS"  # Local DB we created on the Laptop we'll be using
    db_name = "CanSatDB"
    conn_str = (
        f"Driver={{ODBC Driver 17 for SQL Server}};"
        f"Server={db_server};"
        f"Database={db_name};"
        f"Trusted_Connection=yes;"
    )

    # Try to connect to the database
    try:
        conn = pyodbc.connect(conn_str, autocommit=True)
        cur = conn.cursor()
        print("Connected to DB:", db_name)
    except Exception as err:
        print("DB connect error:", err)
        sys.exit(1)

    # Drop and recreate table (common style)
    try:
        cur.execute("DROP TABLE IF EXISTS telemetry")
        print("Dropped telemetry table")
        cur.execute('''
            CREATE TABLE telemetry (
                id INT PRIMARY KEY IDENTITY(1,1),
                temperature DECIMAL(10,6),
                pressure DECIMAL(10,6),
                altitude DECIMAL(10,6),
                uv_index DECIMAL(10,6),
                CO2 DECIMAL(10,6),
                LON DECIMAL(10,6),
                LAT DECIMAL(10,6),
                ALT DECIMAL(10,6),
                SPG DECIMAL(10,6)
            )
        ''')
        print("Created telemetry table")
    except Exception as err:
        print("Table error:", err)
        sys.exit(1)

    # Try to connect to the serial port
    tries = 5
    while tries > 0:
        try:
            ser = serial.Serial(port, baud, timeout=1)
            print("Connected to serial port:", port)
            break
        except Exception as err:
            tries -= 1
            print("Serial connect error:", err)
            time.sleep(2)
            if tries == 0:
                print("No more tries for serial port.")
                sys.exit(1)

    print("Listening on serial...")

    # Start thread to catch Ctrl+D
    t = threading.Thread(target=wait_for_ctrl_d, daemon=True)
    t.start()

    try:
        while True:
            # If Ctrl+D was pressed, send "ok" to Arduino and break loop
            if restart_flag:
                print("EOF detected, sending ok to Arduino...")
                ser.write(b"ok\n")
                break

            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                print("Got data:", line)
                try:
                    # Expect 9 comma-separated values
                    nums = list(map(float, line.split(",")))
                    if len(nums) == 9:
                        cur.execute('''
                            INSERT INTO telemetry (temperature, pressure, altitude, uv_index, CO2, LON, LAT, ALT, SPG)
                            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
                        ''', nums)
                        conn.commit()
                        print("Data inserted")
                    else:
                        print("Wrong number of values:", len(nums))
                except Exception as err:
                    print("Error parsing data:", err, "Line:", line)
    except KeyboardInterrupt:
        print("Keyboard interrupt, quitting...")
    finally:
        try:
            ser.close()
            conn.close()
            print("Closed serial and DB connections")
        except Exception as err:
            print("Error closing:", err)

    print("Restarting program...")
    time.sleep(1)
    main()  # Restart the program

if __name__ == "__main__":
    main()
