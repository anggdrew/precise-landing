// New UW Mahony AHRS for the LSM9DS1  S.J. Remington 3/2021
// Requires the Sparkfun LSM9DS1 library
// Standard sensor orientation X North (yaw=0), Y West, Z up
// NOTE: Sensor X axis is remapped to the opposite direction of the "X arrow" on the Adafruit sensor breakout!

// New Mahony filter error scheme uses Up (accel Z axis) and West (= Acc X Mag) as the orientation reference vectors

// Both the accelerometer and magnetometer MUST be properly calibrated for this program to work.
// Follow the procedure described in http://sailboatinstruments.blogspot.com/2011/08/improved-magnetometer-calibration.html
// or in more detail, the tutorial https://thecavepearlproject.org/2015/05/22/calibrating-any-compass-or-accelerometer-for-arduino/
//
// To collect data for calibration, use the companion program LSM9DS1_cal_data
//
/*
  Adafruit 3V or 5V board
  Hardware setup: This library supports communicating with the
  LSM9DS1 over either I2C or SPI. This example demonstrates how
  to use I2C. The pin-out is as follows:
  LSM9DS1 --------- Arduino
   SCL ---------- SCL (A5 on older 'Duinos')
   SDA ---------- SDA (A4 on older 'Duinos')
   VIN ------------- 5V
   GND ------------- GND

   CSG, CSXM, SDOG, and SDOXM should all be pulled high.
   pullups on the ADAFRUIT breakout board do this.
*/
// The SFE_LSM9DS1 library requires both Wire and SPI be
// included BEFORE including the 9DS1 library.
#include <Wire.h>
#include <SPI.h>
#include <SparkFunLSM9DS1.h>

#include "Arduino.h"
/* For the bluetooth funcionality */
#include <ArduinoBLE.h>

// #define USE_SERIAL

struct data
{
	float acc_x;
	float acc_y;
	float acc_z;
	float yaw;
	float pitch;
	float roll;
	uint32_t ts;
};


/* Device name which can be scene in BLE scanning software. */
#define BLE_DEVICE_NAME                "Arduino Nano 33 BLE"
/* Local name which should pop up when scanning for BLE devices. */
#define BLE_LOCAL_NAME                "Arduino Nano 33 BLE Sensors"

/*
 * Declares the BLEService and characteristics we will need for the BLE
 * transfer. The UUID was randomly generated using one of the many online
 * tools that exist. It was chosen to use BLECharacteristic instead of
 * BLEFloatCharacteristic (and other characteristic types) as it is hard
 * to view non-string data in most BLE scanning software. Strings can be
 * viewed easiler enough. In an actual application you might want to
 * transfer specific data types directly.
 */
BLEService BLESensors("590d65c7-3a0a-4023-a05a-6aaf2f22441c");
BLECharacteristic ble_data("0001", BLERead | BLENotify | BLEBroadcast,
			   sizeof(struct data));

//////////////////////////
// LSM9DS1 Library Init //
//////////////////////////
// default settings gyro  245 d/s, accel = 2g, mag = 4G
LSM9DS1 imu;

// VERY IMPORTANT!
//These are the previously determined offsets and scale factors for accelerometer and magnetometer, using MPU9250_cal and Magneto
//The filter will produce meaningless results if these data are not correct

//Gyro scale 245 dps convert to radians/sec and offsets
float Gscale = (M_PI / 180.0) * 0.00875; //245 dps scale sensitivity = 8.75 mdps/LSB
int G_offset[3] = {75, 31, 142};

//Accel scale 16457.0 to normalize
float A_B[3]
{ -133.33,   72.29, -291.92};

float A_Ainv[3][3]
{ {  1.00260,  0.00404,  0.00023},
  {  0.00404,  1.00708,  0.00263},
  {  0.00023,  0.00263,  0.99905}
};

//Mag scale 3746.0 to normalize
float M_B[3]
{ -922.31, 2199.41,  373.17};

float M_Ainv[3][3]
{ {  1.04492,  0.03452, -0.01714},
  {  0.03452,  1.05168,  0.00644},
  { -0.01714,  0.00644,  1.07005}
};

// local magnetic declination in degrees
float declination = -14.84;

// These are the free parameters in the Mahony filter and fusion scheme,
// Kp for proportional feedback, Ki for integral
// Kp is not yet optimized. Ki is not used.
#define Kp 50.0
#define Ki 0.0

unsigned long now = 0, last = 0; //micros() timers for AHRS loop
float deltat = 0;  //loop time in seconds

// Vector to hold quaternion
static float q[4] = {1.0, 0.0, 0.0, 0.0};
static float yaw, pitch, roll; //Euler angle output

static void imuTransformVectorBodyToEarth(float &ax, float &ay, float &az);

void setup()
{
     int i;

     for (i = 5; i > 0; i--) {
            digitalWrite(LED_BUILTIN, HIGH);   // turn the LED on (HIGH is the voltage level)
            delay(100);                       // wait for a second
            digitalWrite(LED_BUILTIN, LOW);    // turn the LED off by making the voltage LOW
            delay(100);
     }

#ifdef USE_SERIAL
     Serial.begin(115200);
     while (!Serial); //wait for connection
     Serial.println();
     Serial.println("LSM9DS1 AHRS starting");
#endif

     Wire1.begin();

     if (imu.begin(LSM9DS1_AG_ADDR(1), LSM9DS1_M_ADDR(1), Wire1) == false) {
#ifdef USE_SERIAL
	     Serial.println(F("LSM9DS1 not detected"));
#endif
	     while (1);
     }


    /* BLE Setup. For information, search for the many ArduinoBLE examples.*/
    if (!BLE.begin())  {
        while (1) {
            digitalWrite(LED_BUILTIN, HIGH);   // turn the LED on (HIGH is the voltage level)
            delay(20);                       // wait for a second
            digitalWrite(LED_BUILTIN, LOW);    // turn the LED off by making the voltage LOW
            delay(20);
       }
    }
    else {
        BLE.setDeviceName(BLE_DEVICE_NAME);
        BLE.setLocalName(BLE_LOCAL_NAME);
        BLE.setAdvertisedService(BLESensors);
        /* A seperate characteristic is used for each sensor data type. */
        BLESensors.addCharacteristic(ble_data);

        BLE.addService(BLESensors);
        BLE.advertise();

#ifdef USE_SERIAL
        String address = BLE.address();
        Serial.print("Local MAC address is: ");
        Serial.println(address);
#endif
    }

}

void loop()
{
  static char updated = 0; //flags for sensor updates
  static int loop_counter=0; //sample & update loop counter
  static float Gxyz[3], Axyz[3], Mxyz[3]; //centered and scaled gyro/accel/mag data

  BLEDevice central = BLE.central();

  if (!central)
	  return;
  if (!central.connected())
	  return;

  // Update the sensor values whenever new data is available
  if ( imu.accelAvailable() ) {
    updated |= 1;  //acc updated
    imu.readAccel();
  }
  if ( imu.magAvailable() ) {
    updated |= 2; //mag updated
    imu.readMag();
  }
  if ( imu.gyroAvailable() ) {
    updated |= 4; //gyro updated
    imu.readGyro();
  }
  if (updated == 7) //all sensors updated?
  {
    updated = 0; //reset update flags
    loop_counter++;
    get_scaled_IMU(Gxyz, Axyz, Mxyz);

    // correct accel/gyro handedness
    // Note: the illustration in the LSM9DS1 data sheet implies that the magnetometer
    // X and Y axes are rotated with respect to the accel/gyro X and Y, but this is not case.

    Axyz[0] = -Axyz[0]; //fix accel/gyro handedness
    Gxyz[0] = -Gxyz[0]; //must be done after offsets & scales applied to raw data

    now = micros();
    deltat = (now - last) * 1.0e-6; //seconds since last update
    last = now;

    MahonyQuaternionUpdate(Axyz[0], Axyz[1], Axyz[2], Gxyz[0], Gxyz[1], Gxyz[2],
                           Mxyz[0], Mxyz[1], Mxyz[2], deltat);

    // Define Tait-Bryan angles.
    // Standard sensor orientation : X magnetic North, Y West, Z Up (NWU)
    // this code corrects for magnetic declination.
    // Pitch is angle between sensor x-axis and Earth ground plane, toward the
    // Earth is positive, up toward the sky is negative. Roll is angle between
    // sensor y-axis and Earth ground plane, y-axis up is positive roll.
    // Tait-Bryan angles as well as Euler angles are
    // non-commutative; that is, the get the correct orientation the rotations
    // must be applied in the correct order.
    //
    // http://en.wikipedia.org/wiki/Conversion_between_quaternions_and_Euler_angles
    // which has additional links.
    roll  = atan2((q[0] * q[1] + q[2] * q[3]), 0.5 - (q[1] * q[1] + q[2] * q[2]));
    pitch = asin(2.0 * (q[0] * q[2] - q[1] * q[3]));
    yaw   = atan2((q[1] * q[2] + q[0] * q[3]), 0.5 - ( q[2] * q[2] + q[3] * q[3]));
    // to degrees
    yaw   *= 180.0 / PI;
    pitch *= 180.0 / PI;
    roll *= 180.0 / PI;

    // http://www.ngdc.noaa.gov/geomag-web/#declination
    //conventional nav, yaw increases CW from North, corrected for local magnetic declination

    yaw = -(yaw + declination);
    if (yaw < 0) yaw += 360.0;
    if (yaw >= 360.0) yaw -= 360.0;

    /* Back to radians */
    yaw /= 180.0 / PI;
    pitch /= 180.0 / PI;
    roll /= 180.0 / PI;

#ifdef USE_SERIAL
    Serial.print("ypr ");
    Serial.print(yaw, 0);
    Serial.print(", ");
    Serial.print(pitch, 0);
    Serial.print(", ");
    Serial.print(roll, 0);
//    Serial.print(", ");  //prints 24 in 300 ms (80 Hz) with 16 MHz ATmega328
//    Serial.print(loop_counter);  //sample & update loops per print interval
    loop_counter = 0;
    Serial.print(", ");
#endif

    float acc_x = imu.calcAccel(imu.ax);
    float acc_y = imu.calcAccel(imu.ay);
    float acc_z = imu.calcAccel(imu.az);

    /* fix accel handedness */
    acc_x = -acc_x;

    imuTransformVectorBodyToEarth(acc_x, acc_y, acc_z);

    /* compensate 1g */
    acc_z -= 1;

#ifdef USE_SERIAL
    Serial.print("acc ");
    Serial.print(acc_x, 2);
    Serial.print(", ");
    Serial.print(acc_y, 2);
    Serial.print(", ");
    Serial.print(acc_z, 2);
    Serial.println();
#endif

    struct data data = {
	    .acc_x  = acc_x,
	    .acc_y  = acc_y,
	    .acc_z  = acc_z,
	    .yaw    = yaw,
	    .pitch  = pitch,
	    .roll   = roll,
	    .ts     = now,
    };
    ble_data.writeValue(&data, sizeof(data), false);
  }
}

// vector math
float vector_dot(float a[3], float b[3])
{
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

void vector_normalize(float a[3])
{
  float mag = sqrt(vector_dot(a, a));
  a[0] /= mag;
  a[1] /= mag;
  a[2] /= mag;
}




static float rMat[3][3];

static void imuComputeRotationMatrix(void)
{
    float q0 = q[0], q1 = q[1], q2 = q[2], q3 = q[3];

    float q1q1 = sq(q1);
    float q2q2 = sq(q2);
    float q3q3 = sq(q3);

    float q0q1 = q0 * q1;
    float q0q2 = q0 * q2;
    float q0q3 = q0 * q3;
    float q1q2 = q1 * q2;
    float q1q3 = q1 * q3;
    float q2q3 = q2 * q3;

    rMat[0][0] = 1.0f - 2.0f * q2q2 - 2.0f * q3q3;
    rMat[0][1] = 2.0f * (q1q2 + -q0q3);
    rMat[0][2] = 2.0f * (q1q3 - -q0q2);

    rMat[1][0] = 2.0f * (q1q2 - -q0q3);
    rMat[1][1] = 1.0f - 2.0f * q1q1 - 2.0f * q3q3;
    rMat[1][2] = 2.0f * (q2q3 + -q0q1);

    rMat[2][0] = 2.0f * (q1q3 + -q0q2);
    rMat[2][1] = 2.0f * (q2q3 - -q0q1);
    rMat[2][2] = 1.0f - 2.0f * q1q1 - 2.0f * q2q2;

}


static void imuTransformVectorBodyToEarth(float &ax, float &ay, float &az)
{
    /* From body frame to earth frame */
    float x = rMat[0][0] * ax + rMat[0][1] * ay + rMat[0][2] * az;
    float y = rMat[1][0] * ax + rMat[1][1] * ay + rMat[1][2] * az;
    float z = rMat[2][0] * ax + rMat[2][1] * ay + rMat[2][2] * az;

    ax = x;
    ay = y;
    az = z;
}



// function to subtract offsets and apply scale/correction matrices to IMU data

void get_scaled_IMU(float Gxyz[3], float Axyz[3], float Mxyz[3]) {
  byte i;
  float temp[3];
  Gxyz[0] = Gscale * (imu.gx - G_offset[0]);
  Gxyz[1] = Gscale * (imu.gy - G_offset[1]);
  Gxyz[2] = Gscale * (imu.gz - G_offset[2]);

  Axyz[0] = imu.ax;
  Axyz[1] = imu.ay;
  Axyz[2] = imu.az;
  Mxyz[0] = imu.mx;
  Mxyz[1] = imu.my;
  Mxyz[2] = imu.mz;

  //apply accel offsets (bias) and scale factors from Magneto

  for (i = 0; i < 3; i++) temp[i] = (Axyz[i] - A_B[i]);
  Axyz[0] = A_Ainv[0][0] * temp[0] + A_Ainv[0][1] * temp[1] + A_Ainv[0][2] * temp[2];
  Axyz[1] = A_Ainv[1][0] * temp[0] + A_Ainv[1][1] * temp[1] + A_Ainv[1][2] * temp[2];
  Axyz[2] = A_Ainv[2][0] * temp[0] + A_Ainv[2][1] * temp[1] + A_Ainv[2][2] * temp[2];
  vector_normalize(Axyz);

  //apply mag offsets (bias) and scale factors from Magneto

  for (int i = 0; i < 3; i++) temp[i] = (Mxyz[i] - M_B[i]);
  Mxyz[0] = M_Ainv[0][0] * temp[0] + M_Ainv[0][1] * temp[1] + M_Ainv[0][2] * temp[2];
  Mxyz[1] = M_Ainv[1][0] * temp[0] + M_Ainv[1][1] * temp[1] + M_Ainv[1][2] * temp[2];
  Mxyz[2] = M_Ainv[2][0] * temp[0] + M_Ainv[2][1] * temp[1] + M_Ainv[2][2] * temp[2];
  vector_normalize(Mxyz);
}

// Mahony orientation filter, assumed World Frame NWU (xNorth, yWest, zUp)
// Modified from Madgwick version to remove Z component of magnetometer:
// The two reference vectors are now Up (Z, Acc) and West (Acc cross Mag)
// sjr 3/2021
// input vectors ax, ay, az and mx, my, mz MUST be normalized!
// gx, gy, gz must be in units of radians/second
//
void MahonyQuaternionUpdate(float ax, float ay, float az, float gx, float gy, float gz, float mx, float my, float mz, float deltat)
{
  // Vector to hold integral error for Mahony method
  static float eInt[3] = {0.0, 0.0, 0.0};
    // short name local variable for readability
  float q1 = q[0], q2 = q[1], q3 = q[2], q4 = q[3];
  float norm;
  float hx, hy, hz;  //observed West horizon vector W = AxM
  float ux, uy, uz, wx, wy, wz; //calculated A (Up) and W in body frame
  float ex, ey, ez;
  float pa, pb, pc;

  // Auxiliary variables to avoid repeated arithmetic
  float q1q1 = q1 * q1;
  float q1q2 = q1 * q2;
  float q1q3 = q1 * q3;
  float q1q4 = q1 * q4;
  float q2q2 = q2 * q2;
  float q2q3 = q2 * q3;
  float q2q4 = q2 * q4;
  float q3q3 = q3 * q3;
  float q3q4 = q3 * q4;
  float q4q4 = q4 * q4;

  // Measured horizon vector = a x m (in body frame)
  hx = ay * mz - az * my;
  hy = az * mx - ax * mz;
  hz = ax * my - ay * mx;
  // Normalise horizon vector
  norm = sqrt(hx * hx + hy * hy + hz * hz);
  if (norm == 0.0f) return; // Handle div by zero

  norm = 1.0f / norm;
  hx *= norm;
  hy *= norm;
  hz *= norm;

  // Estimated direction of Up reference vector
  ux = 2.0f * (q2q4 - q1q3);
  uy = 2.0f * (q1q2 + q3q4);
  uz = q1q1 - q2q2 - q3q3 + q4q4;

  // estimated direction of horizon (West) reference vector
  wx = 2.0f * (q2q3 + q1q4);
  wy = q1q1 - q2q2 + q3q3 - q4q4;
  wz = 2.0f * (q3q4 - q1q2);

  // Error is the summed cross products of estimated and measured directions of the reference vectors
  // It is assumed small, so sin(theta) ~ theta IS the angle required to correct the orientation error.

  ex = (ay * uz - az * uy) + (hy * wz - hz * wy);
  ey = (az * ux - ax * uz) + (hz * wx - hx * wz);
  ez = (ax * uy - ay * ux) + (hx * wy - hy * wx);
 
  if (Ki > 0.0f)
  {
    eInt[0] += ex;      // accumulate integral error
    eInt[1] += ey;
    eInt[2] += ez;
    // Apply I feedback
    gx += Ki * eInt[0];
    gy += Ki * eInt[1];
    gz += Ki * eInt[2];
  }


  // Apply P feedback
  gx = gx + Kp * ex;
  gy = gy + Kp * ey;
  gz = gz + Kp * ez;

  // Integrate rate of change of quaternion
  pa = q2;
  pb = q3;
  pc = q4;
  q1 = q1 + (-q2 * gx - q3 * gy - q4 * gz) * (0.5f * deltat);
  q2 = pa + (q1 * gx + pb * gz - pc * gy) * (0.5f * deltat);
  q3 = pb + (q1 * gy - pa * gz + pc * gx) * (0.5f * deltat);
  q4 = pc + (q1 * gz + pa * gy - pb * gx) * (0.5f * deltat);

  // Normalise quaternion
  norm = sqrt(q1 * q1 + q2 * q2 + q3 * q3 + q4 * q4);
  norm = 1.0f / norm;
  q[0] = q1 * norm;
  q[1] = q2 * norm;
  q[2] = q3 * norm;
  q[3] = q4 * norm;

  // Pre-compute rotation matrix from quaternion
  imuComputeRotationMatrix();

  
}
