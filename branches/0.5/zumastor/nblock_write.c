/*
 * Perform nonblocking write to a file (useful for FIFO).
 *
 * Copyright 2007 Google Inc.
 * Author: Chuan-kai Lin (cklin@google.com)
 */

#include <fcntl.h>
#include <unistd.h>

#define BUF_SIZE 64

int main(int argc, const char *argv[])
{
  int fifo, read_size;
  char buffer[BUF_SIZE];

  if (argc != 2)  return 1;
  fifo = open(argv[1], O_WRONLY | O_NONBLOCK);
  if (fifo == -1)  return 2;
  for ( ; ; ) {
    read_size = read(STDIN_FILENO, buffer, BUF_SIZE);
    if (read_size <= 0)  break;
    write(fifo, buffer, read_size);
  }
  close(fifo);
  return 0;
}
