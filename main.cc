#include "graphics.h"
#include <iostream>
#include <string>
#include <thread>
#include <map>
#include <ctime>
#include <cmath>
#include <cstdlib>
#include <fstream>

mouse_msg gMouseMsg = { 0 };
key_msg gKeyMsg = { 0 };
std::map <std::string, PIMAGE> res;


// 加载函数，用于初始化资源
void load (void);
// 事件更新函数，主要用来接收事件，比如鼠标键盘事件
void eventUpdate (void);
// 输出更新函数，主要用于处理输入
void dataUpdate (void);
// 画面绘制函数，主要用于显示输出
void drawInterface (void);

int main (int argc, char *argv []) {
	// 手动刷新模式
	setinitmode (INIT_RENDERMANUAL);
	// 界面分辨率
	initgraph (1800, 1600);
	// setbkmode (TRANSPARENT);
	load ();
	for (;is_run (); delay_fps (60), cleardevice ()) {
		
		eventUpdate ();
		dataUpdate ();
		drawInterface ();
	}
	
	closegraph ();
	return 0;
}

void load (void) {
	// 从外部读入文件名和路径数据，然后往map中写入数据
	PIMAGE tmp = newimage ();
	getimage_pngfile (tmp, "chesses.png");
	std::fstream file;
	file.open ("chesses.txt", std::ios::in);
	std::string str;
	int x, y, w, h;
	while (file >> str >> x >> y >> w >> h) {
		if (res.count(str)==0) {
			res.insert ({str, newimage()});
			getimage (res [str], tmp, x, y, w, h);
		} else {
			throw "you have inserted same name image!";
		}
	}
	file.close ();
	delimage (tmp);
	srand(time(nullptr));
}
void eventUpdate (void) {
//
	while (mousemsg ()) {
		gMouseMsg = getmouse ();
	}
	while (kbmsg ()) {
		gKeyMsg = getkey ();
	}
}
void dataUpdate (void) {
//
}
void drawInterface (void) {
//
setfillcolor (EGERGB(247,165,47));
bar (0,0,getwidth(),getheight());

for (int i = 0; i < getwidth()/32; i++) {
	for (int j = 0; j < getheight()/32; j++) {
		switch (rand()%3) {
			case 0:
				putimage_withalpha (nullptr, res["white"], i*32, j*32);
			break;
			case 1:
				putimage_withalpha (nullptr, res["black"], i*32, j*32);
			break;
			case 2:
				putimage_withalpha(nullptr, res["chineseChess"], i*32, j*32);
				switch (rand()%14) {
					case 0:
						putimage_withalpha (nullptr, res["shuai1"], i*32, j*32);
					break;
					case 1:
						putimage_withalpha (nullptr, res["shi1"], i*32, j*32);
					break;
					case 2:
						putimage_withalpha (nullptr, res["xiang1"], i*32, j*32);
					break;
					case 3:
						putimage_withalpha (nullptr, res["ma1"], i*32, j*32);
					break;
					case 4:
						putimage_withalpha (nullptr, res["che1"], i*32, j*32);
					break;
					case 5:
						putimage_withalpha (nullptr, res["shuai2"], i*32, j*32);
					break;
					case 6:
						putimage_withalpha (nullptr, res["bing"], i*32, j*32);
					break;
					case 7:
						putimage_withalpha (nullptr, res["jiang"], i*32, j*32);
					break;
					case 8:
						putimage_withalpha (nullptr, res["shi2"], i*32, j*32);
					break;
					case 9:
						putimage_withalpha (nullptr, res["xiang2"], i*32, j*32);
					break;
					case 10:
						putimage_withalpha (nullptr, res["ma2"], i*32, j*32);
					break;
					case 11:
						putimage_withalpha (nullptr, res["che2"], i*32, j*32);
					break;
					case 12:
						putimage_withalpha (nullptr, res["pao"], i*32, j*32);
					break;
					case 13:
						putimage_withalpha (nullptr, res["zu"], i*32, j*32);
					break;
				}
			break;
		}
	}
}

}
