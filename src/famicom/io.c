#include "famicom/io.h"
#include "famicom/famicom.h"
#include <stdio.h>

void IO_HandleInput(int arg) {
	switch (arg) {
		case IO_KEY_RESET:
			Famicom_Reset();
			break;
	}
}
