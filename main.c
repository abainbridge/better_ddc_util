#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

#define DDC_CI_ADDR 0x37         // Standard DDC/CI I2C slave address
#define VCP_BRIGHTNESS 0x10      // Feature code for Luminance/Brightness

// DDC/CI packets require a simple XOR checksum of all preceding bytes
unsigned char calculate_checksum(unsigned char *buf, int len) {
    unsigned char crc = 0;
    for (int i = 0; i < len; i++) {
        crc ^= buf[i];
    }
    return crc;
}

int get_brightness(int fd, int *current, int *max_val) {
    // DDC/CI Get VCP packet structure:
    // [Source/Control (0x51)] [Length (0x82)] [Opcode (0x01)] [Feature (0x10)] [Checksum]
    unsigned char packet[5];
    packet[0] = 0x51;
    packet[1] = 0x82;          // Length: 2 payload bytes follow
    packet[2] = 0x01;          // Opcode: Get VCP Feature
    packet[3] = VCP_BRIGHTNESS;// Feature: 0x10 (Brightness)
    packet[4] = calculate_checksum(packet, 4);

    if (write(fd, packet, 5) != 5) {
        perror("Failed to write Get VCP packet");
        return -1;
    }

    // Give the monitor a brief moment to process the query
    usleep(50000);

    // Read response packet from the monitor
    unsigned char resp[12];
    int bytes_read = read(fd, resp, sizeof(resp));
    if (bytes_read < 10) {
        perror("Failed to read valid response from monitor");
        return -1;
    }

    // Verify response headers:
    // resp[2] -> 0x02 (VCP Feature Reply)
    // resp[3] -> 0x00 (No error)
    // resp[4] -> 0x10 (Feature code)
    if (resp[2] != 0x02 || resp[3] != 0x00 || resp[4] != VCP_BRIGHTNESS) {
        fprintf(stderr, "Invalid or error response received from monitor\n");
        return -1;
    }

    // Parse Maximum and Current values (2 bytes each: High Byte, Low Byte)
    *max_val = (resp[6] << 8) | resp[7];
    *current = (resp[8] << 8) | resp[9];

    return 0;
}

int set_brightness_absolute(int fd, unsigned char brightness) {
    // DDC/CI Set VCP packet structure:
    // [Source/Control (0x51)] [Length (0x84)] [Command (0x03)] [Feature (0x10)] [High Byte (0x00)] [Value] [Checksum]
    unsigned char packet[7];
    packet[0] = 0x51;
    packet[1] = 0x84;                    // Length indicator (4 payload bytes)
    packet[2] = 0x03;                    // Opcode: Set VCP Feature
    packet[3] = VCP_BRIGHTNESS;          // Feature: 0x10 (Brightness)
    packet[4] = 0x00;                    // High byte
    packet[5] = brightness;              // Target brightness value
    packet[6] = calculate_checksum(packet, 6);

    if (write(fd, packet, 7) != 7) {
        perror("Failed to write Set VCP packet");
        return -1;
    }

    // Recovery delay required by hardware specs
    usleep(40000);
    return 0;
}

int main(int argc, char const *argv[]) {
    if (argc != 3) {
        printf("Usage: %s <i2c device> <brightness increment>\n\n", argv[0]);
        printf("eg: %s /dev/i2c-2 10+\n", argv[0]);
        printf("or: %s /dev/i2c-2 10-\n", argv[0]);
        return 0;
    }

    char const *i2c_dev = argv[1];
    size_t argv2_len = strlen(argv[2]);
    int brightness_increment = strtol(argv[2], NULL, 10);
    if (argv[2][argv2_len - 1] == '-')
        brightness_increment = -brightness_increment;

    int fd = open(i2c_dev, O_RDWR);
    if (fd < 0) {
        printf("Failed to open I2C device (%s) because '%s'", i2c_dev, strerror(errno));
        return 1;
    }

    if (ioctl(fd, I2C_SLAVE, DDC_CI_ADDR) < 0) {
        perror("Failed to set I2C slave address");
        return 1;
    }

    int current_brightness = 0;
    int max_brightness = 100;

    printf("Opening device %s...\n", i2c_dev);
    if (get_brightness(fd, &current_brightness, &max_brightness) == 0) {
        printf("Current Brightness: %d / %d\n", current_brightness, max_brightness);
    } else {
        fprintf(stderr, "Failed to retrieve current brightness. Exiting.\n");
        return 1;
    }

    printf("Incrementing brightness by %d\n", brightness_increment);
    int new_brightness = current_brightness + brightness_increment;
    if (new_brightness > max_brightness)
        new_brightness = max_brightness;

    printf("Setting brightness to %d...\n", new_brightness);
    if (set_brightness_absolute(fd, (unsigned char)new_brightness) == 0)
        printf("Brightness updated successfully!\n");

    return 0;
}
