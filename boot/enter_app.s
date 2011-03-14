/* Enter the application. */
.section ".text"
.thumb_func
.globl enter_app
.type enter_app, %function

enter_app:
	mov r0, sp
	bx r1
