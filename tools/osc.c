/*
 *  Copyright (C) 2004 Steve Harris, Uwe Koloska
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  $Id: example_client.c,v 1.1.1.1 2004/08/07 22:21:02 theno23 Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "lo/lo.h"

int main(int argc, char *argv[]) {
	if (argc != 3) {
		printf("Usage: %s path value\n", argv[0]);
		return 1;
	}

	/* an address to send messages to. sometimes it is better to let the server
	 * pick a port number for you by passing NULL as the last argument */
	lo_address t = lo_address_new("169.254.13.29", "60000");

	return lo_send(t, argv[1], "i", atoi(argv[2]));
}
