#include<bits/stdc++.h>

const int SIZE=(1<<5);

#include "ReorderBuffer.hpp"
#include "LoadStoreBuffer.hpp"
#include "RegisterStation.hpp"
#include "InstructionQueue.hpp"

using namespace std;

unsigned int HEX_DEC(char a){
	if(isdigit(a))return a-'0';
	else return 10+a-'A';
}

//get the immediate
unsigned int get_imm(unsigned int *imm,bool opt,int p=0){ // opt 0:sign-extend 1:zero-extend
	unsigned int res=0;
	if(opt==0)for(int i=p+1;i<32;++i)imm[i]=imm[p];
	else for(int i=0;i<p;++i)imm[i]=0;
	for(int i=31;i>=0;--i)res=(res<<1)+imm[i];
	return res;
}
unsigned int sext(unsigned int x,unsigned int len){
	unsigned int p=(x&(1<<(len-1)))?1:0;
	unsigned int res=0,xlen=32-len;
	for(int i=1;i<=(int)xlen;++i)res=res<<1|p;
	return res<<len|x;
}


class simulator{
private:
	//pc
	unsigned int pc;

	//Memory
	unsigned char Mem[5000100];

	//IQ
	InstructionQueue IFqueue;
	unsigned int IQ_pc;

	//ROB
	ReorderBuffer ROB_now,ROB_las;
	ROB_node ROB_nxt;

	//RS
	RegisterStation RS_now,RS_las;
	RS_node RS_nxt;

	//LSB
	LoadStoreBuffer LSB_now,LSB_las;
	LSB_node LSB_nxt;

	//Reg
	struct Regfile{
		unsigned int V,Q;
		Regfile(){
			V=0;Q=-1;
		}
		void clear(){
			V=0;Q=-1;
		}
	}Reg_now[SIZE],Reg_las[SIZE];

	//commit
	ROB_node Com;
	unsigned int Com_res;
	struct Commit{
		unsigned int rd,Value,Reorder;
		bool End;
		Commit(){
			End=false;
			rd=Reorder=-1;Value=0;
		}
		void clear(){
			End=false;
			rd=Reorder=-1;Value=0;
		}
	}WR,WQ;

	//CDB
	struct CommonDataBus{
		unsigned int Reorder;
		unsigned int Value;
		unsigned int Jump;
		CommonDataBus(){
			Reorder=Value=Jump=-1;
		}
		void clear(){
			Reorder=Value=Jump=-1;
		}
	}CDB_LSB_now,CDB_LSB_las,CDB_EX,WM,ROB_LSB_las,ROB_LSB_now;

	//EX
	RS_node Exc;

public:

	simulator(){
		pc=0;
		memset(Mem,0,sizeof(Mem));
	}

	void read(){
		char s[15];
		unsigned int pos;
		while(scanf("%s",s)!=EOF)
			if(s[0]=='@'){
				pos=0;
				for(int i=1;i<=8;++i)pos=(pos<<4)+HEX_DEC(s[i]);
			}
			else Mem[pos++]=(HEX_DEC(s[0])<<4)+HEX_DEC(s[1]);
	}

	void ROB_handle(){
		Com.clear();
		ROB_LSB_now.clear();

		ROB_now=ROB_las;

		if((int)Com_res==-1){//pc error - clear
			ROB_now.clear();
			ROB_nxt.clear();
			return ;
		} else if(Com_res==1){//commit success
			ROB_now.pop();
		}

		//use the information from CDB to update the data
		if((int)CDB_EX.Reorder!=-1){
			ROB_node ROB_tmp=ROB_las.take(CDB_EX.Reorder);
			ROB_tmp.Value=CDB_EX.Value;
			ROB_tmp.Jump=CDB_EX.Jump;
			ROB_tmp.Ready=1;
			ROB_now.update(CDB_EX.Reorder,ROB_tmp);
			ROB_LSB_now.Reorder=CDB_EX.Reorder;
			ROB_LSB_now.Value=CDB_EX.Value;
		}
		if((int)CDB_LSB_las.Reorder!=-1){
			ROB_node ROB_tmp=ROB_las.take(CDB_LSB_las.Reorder);
			ROB_tmp.Value=CDB_LSB_las.Value;
			ROB_tmp.Ready=1;
			ROB_now.update(CDB_LSB_las.Reorder,ROB_tmp);
		}

		//check the head of the queue
		if(!ROB_now.empty()){
			ROB_node ROB_tmp=ROB_now.front();
			// if(ROB_tmp.Dest==0){
			// 	printf("[ERROR - Dest=0] pc: %d\n",ROB_tmp.pc);
			// }
			// printf("ROB:   id: %d Inst: %d pc: %d\n",ROB_tmp.id,ROB_tmp.Inst,ROB_tmp.pc);
			if(ROB_tmp.Inst==0b0100011||ROB_tmp.Ready)Com=ROB_tmp;
		}
		// printf("COM1:   id: %d Inst: %d Jump: %d pc: %d\n",Com.id,Com.Inst,Com.Jump,Com.pc);

		//add the new instruction
		if(ROB_nxt.Inst!=0)ROB_now.push(ROB_nxt);
		ROB_nxt.clear();

		// printf("ROB_now[4]'s info Value: %d Ready: %d\n",ROB_now.take(4).Value,ROB_now.take(4).Ready);
		// printf("ROB_las[4]'s info Value: %d Ready: %d\n",ROB_las.take(4).Value,ROB_las.take(4).Ready);
	}

	void LSB_handle(){
		CDB_LSB_now.clear();

		if((int)Com_res==-1){//pc error - clear
			LSB_now.clear();
			LSB_nxt.clear();
			int head=LSB_las.gethead();
			// puts("!");
			for(int i=0;i<SIZE;++i){
				LSB_node LSB_tmp=LSB_las.take((head+i)%SIZE);
				// printf("|LSB|clear Op: %d Reorder: %d pc: %d Commit: %d\n",LSB_tmp.Op,LSB_tmp.Reorder,LSB_tmp.pc,LSB_tmp.Commit);
				if(LSB_tmp.Op==0)break;
				if(LSB_tmp.Commit)LSB_now.push(LSB_tmp);
			}
		} else LSB_now=LSB_las;

		//use the information from ROB to update the store instruction
		if((int)WM.Reorder!=-1){
			for(int i=0;i<SIZE;++i){
				LSB_node LSB_tmp=LSB_now.take(i);
//				printf("|LSB|   %d %d %d %d\n",i,LSB_tmp.Op,LSB_tmp.Reorder,LSB_tmp.pc);
				if(LSB_tmp.Reorder==WM.Reorder){
					//printf("===%d %d\n",i,LSB_now.gethead());
					LSB_tmp.Commit=1;
					// if((int)LSB_tmp.Qj<0||(int)LSB_tmp.Qk<0){
					// 	printf("[ERROR] - Store pc: %d Qj: %d Qk: %d\n",LSB_tmp.pc,LSB_tmp.Qj,LSB_tmp.Qk);
					// 	exit(0);
					// }
					LSB_tmp.Vj=Reg_las[LSB_tmp.Qj].V;
					LSB_tmp.Vk=Reg_las[LSB_tmp.Qk].V;
					LSB_now.update(i,LSB_tmp);
					LSB_tmp=LSB_now.front();
					//printf("-LSB: %d %d\n",LSB_tmp.Op,LSB_tmp.Commit);
					break;
				}
			}
		}
		if((int)ROB_LSB_las.Reorder!=-1){
			for(int i=0;i<SIZE;++i){
				LSB_node LSB_tmp=LSB_now.take(i);
				if(LSB_tmp.Op==0b0000011&&LSB_tmp.Qj==ROB_LSB_las.Reorder){
					LSB_tmp.Qj=-1;
					LSB_tmp.Vj=ROB_LSB_las.Value;
					LSB_now.update(i,LSB_tmp);
				}
			}
		}

		//chech the head of the buffer
		LSB_node LSB_tmp=LSB_now.front();
		// printf("LSB:   Op: %d Reorder: %d pc: %d\n",LSB_tmp.Op,LSB_tmp.Reorder,LSB_tmp.pc);
		if(LSB_tmp.Ready()){
			if(LSB_tmp.Op==0b0000011){//Load
				if(LSB_tmp.func3==0b000){//LB
					unsigned int p=LSB_tmp.Vj+LSB_tmp.A;
					CDB_LSB_now.Reorder=LSB_tmp.Reorder;
					CDB_LSB_now.Value=sext(Mem[p],8);
				} else if(LSB_tmp.func3==0b001){//LH
					unsigned int p=LSB_tmp.Vj+LSB_tmp.A;
					CDB_LSB_now.Reorder=LSB_tmp.Reorder;
					CDB_LSB_now.Value=sext(Mem[p+1]<<8|Mem[p],16);
				} else if(LSB_tmp.func3==0b010){//LW
					unsigned int p=LSB_tmp.Vj+LSB_tmp.A;
					CDB_LSB_now.Reorder=LSB_tmp.Reorder;
					CDB_LSB_now.Value=Mem[p+3]<<24|Mem[p+2]<<16|Mem[p+1]<<8|Mem[p];
				} else if(LSB_tmp.func3==0b100){//LBU
					unsigned int p=LSB_tmp.Vj+LSB_tmp.A;
					CDB_LSB_now.Reorder=LSB_tmp.Reorder;
					CDB_LSB_now.Value=Mem[p];
				} else if(LSB_tmp.func3==0b101){//LHU
					unsigned int p=LSB_tmp.Vj+LSB_tmp.A;
					CDB_LSB_now.Reorder=LSB_tmp.Reorder;
					CDB_LSB_now.Value=Mem[p+1]<<8|Mem[p];
				}
			} else if(LSB_tmp.Op==0b0100011){//Store
				if(LSB_tmp.func3==0b000){//SB
					unsigned int p=LSB_tmp.Vj+LSB_tmp.A;
					Mem[p]=LSB_tmp.Vk&((1<<8)-1);
				} else if(LSB_tmp.func3==0b001){//SH
					unsigned int p=LSB_tmp.Vj+LSB_tmp.A,v=LSB_tmp.Vk;
					for(int i=0;i<2;++i)
						Mem[p+i]=v&((1<<8)-1),v>>=8;
				} else if(LSB_tmp.func3==0b010){//SW
					unsigned int p=LSB_tmp.Vj+LSB_tmp.A,v=LSB_tmp.Vk;
					for(int i=0;i<4;++i)
						Mem[p+i]=v&((1<<8)-1),v>>=8;
				}
			}
			LSB_now.pop();
		}

		//add the new instruction
		if(LSB_nxt.Op!=0)LSB_now.push(LSB_nxt);
		LSB_nxt.clear();
	}

	void RS_handle(){
		RS_now=RS_las;

		if((int)Com_res==-1){//pc error - clear
			RS_now.clear();
			RS_nxt.clear();
			return ;
		}

		//use the information from CDB to update the data
		if((int)CDB_EX.Reorder!=-1){
			for(int i=0;i<SIZE;++i){
				RS_node RS_tmp=RS_now.take(i);
				if(!RS_tmp.Busy)continue;
				if(RS_tmp.Qj==CDB_EX.Reorder){
					RS_tmp.Qj=-1;
					RS_tmp.Vj=CDB_EX.Value;
				}
				if(RS_tmp.Qk==CDB_EX.Reorder){
					RS_tmp.Qk=-1;
					RS_tmp.Vk=CDB_EX.Value;
				}
				RS_now.update(i,RS_tmp);
			}
			if(RS_nxt.Op!=0){
				if(RS_nxt.Qj==CDB_EX.Reorder){
					RS_nxt.Qj=-1;
					RS_nxt.Vj=CDB_EX.Value;
				}
				if(RS_nxt.Qk==CDB_EX.Reorder){
					RS_nxt.Qk=-1;
					RS_nxt.Vk=CDB_EX.Value;
				}
			}
		}
		if((int)CDB_LSB_las.Reorder!=-1){
			for(int i=0;i<SIZE;++i){
				RS_node RS_tmp=RS_now.take(i);
				if(!RS_tmp.Busy)continue;
				if(RS_tmp.Qj==CDB_LSB_las.Reorder){
					RS_tmp.Qj=-1;
					RS_tmp.Vj=CDB_LSB_las.Value;
				}
				if(RS_tmp.Qk==CDB_LSB_las.Reorder){
					RS_tmp.Qk=-1;
					RS_tmp.Vk=CDB_LSB_las.Value;
				}
				RS_now.update(i,RS_tmp);
			}
			if(RS_nxt.Op!=0){
				if(RS_nxt.Qj==CDB_LSB_las.Reorder){
					RS_nxt.Qj=-1;
					RS_nxt.Vj=CDB_LSB_las.Value;
				}
				if(RS_nxt.Qk==CDB_LSB_las.Reorder){
					RS_nxt.Qk=-1;
					RS_nxt.Vk=CDB_LSB_las.Value;
				}
			}
		}

		// {
		// 	RS_node RS_tmp=RS_now.take(1);
		// 	printf("RS[1]'s Vj: %d Qj: %d ----- Reg13's V: %d Q: %d\n",RS_tmp.Vj,RS_tmp.Qj,Reg_las[13].V,Reg_las[13].Q);
		// }

		//find a ready instruction and send it to EX
		Exc.clear();
		for(int i=0;i<SIZE;++i){
			RS_node RS_tmp=RS_now.take(i);
			// if(i==2){
			// 	printf("RS[2]   Op: %d Qj: %d pc: %d\n",RS_tmp.Op,RS_tmp.Qj,RS_tmp.pc);
			// }
			if(RS_tmp.Ready()){
				Exc=RS_tmp;
				// if(Exc.Vj==4484)printf("Reg13's RSid: %d\n",i);
				RS_now.erase(i);
				// printf("RS_find_Ready   i: %d\n",i);
				break;
			}
		}

		//add the new instruction
		if(RS_nxt.Op!=0){
			unsigned int pos=RS_now.getpos();
			RS_now.update(pos,RS_nxt);
			RS_nxt.clear();
		}
	}

	void IF(){
		IQ_pc=-1;
		if(IFqueue.full())return ;
		IQ_pc=pc;
	}

	void regfile(){
		for(int i=0;i<SIZE;++i)Reg_now[i]=Reg_las[i];
		
		// if(WR.rd==0){
		// 	printf("[ERROR - rd=0] Reorder: %d\n",WR.Reorder);
		// }

		if((int)Com_res==-1){//pc error - clear
			for(int i=0;i<SIZE;++i)Reg_now[i].Q=-1;
			if((int)WR.rd!=-1)Reg_now[WR.rd].V=WR.Value;
			return ;
		}

		if((int)WR.rd!=-1){
			// printf("Reg's regfile[begin] Inst: %d Val: %d Reorder: %d\n",WR.rd,WR.Value,WR.Reorder);
			Reg_now[WR.rd].V=WR.Value;
			if(Reg_now[WR.rd].Q==WR.Reorder)
				Reg_now[WR.rd].Q=-1;
		}

		if((int)WQ.rd!=-1){
			Reg_now[WQ.rd].Q=WQ.Value;
		}
	}

	void issue(){
		pc+=4;WQ.clear();

		if(IFqueue.empty())return ;
		if(ROB_las.full())return ;
		int nowpc=IFqueue.front();
		
		unsigned int x=(Mem[nowpc+3]<<24)|(Mem[nowpc+2]<<16)|(Mem[nowpc+1]<<8)|Mem[nowpc];
		bool isEnd=(x==0x0ff00513);
		// cout<<"pc: "<<nowpc<<'\n';
		// for(int i=31;i>=0;--i)cout<<((x&(1<<i))?1:0);
		// cout<<'\n';
		unsigned int opcode=x&((1<<7)-1);x>>=7;
		unsigned int rd=0,rs1=0,rs2=0,func3=0,func7=0,off[32],shamt=0,imm=0;
		memset(off,0,sizeof(off));

		if(opcode==0b0000011){//Load
			rd=x&((1<<5)-1),x>>=5;
			func3=x&((1<<3)-1),x>>=3;
			rs1=x&((1<<5)-1),x>>=5;
			for(int i=0;i<=11;++i)off[i]=x&1,x>>=1;
			imm=get_imm(off,0,11);
			if(rd==0){
				IFqueue.pop();
				return ;
			}
			if(LSB_las.full())return ;
			
			//ROB
			unsigned int ROB_pos=ROB_las.nxtpos();
			ROB_nxt.Ready=0;
			ROB_nxt.Inst=opcode;
			ROB_nxt.Dest=rd;
			ROB_nxt.pc=nowpc;

			//LSB
			LSB_nxt.Op=opcode;
			LSB_nxt.func3=func3;
			LSB_nxt.A=imm;
			LSB_nxt.Busy=1;
			if((int)Reg_las[rs1].Q==-1)LSB_nxt.Vj=Reg_las[rs1].V;
			else {
				ROB_node ROB_tmp=ROB_las.take(Reg_las[rs1].Q);
				if(ROB_tmp.Ready)LSB_nxt.Vj=ROB_tmp.Value;
				else LSB_nxt.Qj=Reg_las[rs1].Q;
			}
			LSB_nxt.Reorder=ROB_pos;
			LSB_nxt.pc=nowpc;

			//Reg
			WQ.rd=rd;WQ.Value=ROB_pos;

		} else if(opcode==0b0100011){//Store
			for(int i=0;i<=4;++i)off[i]=x&1,x>>=1;
			func3=x&((1<<3)-1),x>>=3;
			rs1=x&((1<<5)-1),x>>=5;
			rs2=x&((1<<5)-1),x>>=5;
			for(int i=5;i<=11;++i)off[i]=x&1,x>>=1;
			imm=get_imm(off,0,11);
			if(ROB_las.full())return ;

			//ROB
			unsigned int ROB_pos=ROB_las.nxtpos();
			ROB_nxt.Ready=0;
			ROB_nxt.Inst=opcode;
			ROB_nxt.pc=nowpc;

			//LSB
			LSB_nxt.Op=opcode;
			LSB_nxt.func3=func3;
			LSB_nxt.A=imm;
			LSB_nxt.Busy=1;
			// if(nowpc==4500){
			// 	printf("[ERROR] - issue rs1: %d rs2: %d\n",rs1,rs2);
			// }
			LSB_nxt.Qj=rs1;
			LSB_nxt.Qk=rs2;
			LSB_nxt.Reorder=ROB_pos;
			LSB_nxt.pc=nowpc;

		} else if(opcode==0b0110111){//LUI
			rd=x&((1<<5)-1),x>>=5;
			for(int i=12;i<=31;++i)off[i]=x&1,x>>=1;
			imm=get_imm(off,1);
			if(rd==0){
				IFqueue.pop();
				return ;
			}
			if(RS_las.full())return ;

			//ROB
			unsigned int ROB_pos=ROB_las.nxtpos();
			ROB_nxt.Ready=0;
			ROB_nxt.Inst=opcode;
			ROB_nxt.Dest=rd;
			ROB_nxt.pc=nowpc;
			
			//RS
			RS_nxt.Op=opcode;
			RS_nxt.Busy=1;
			RS_nxt.A=imm;
			RS_nxt.Reorder=ROB_pos;
			RS_nxt.pc=nowpc;
			
			//Reg
			WQ.rd=rd;WQ.Value=ROB_pos;

		} else if(opcode==0b0010111){//AUIPC
			rd=x&((1<<5)-1),x>>=5;
			for(int i=12;i<=31;++i)off[i]=x&1,x>>=1;
			imm=get_imm(off,1);
			if(rd==0){
				IFqueue.pop();
				return ;
			}
			if(RS_now.full())return ;

			//ROB
			unsigned int ROB_pos=ROB_las.nxtpos();
			ROB_nxt.Ready=0;
			ROB_nxt.Inst=opcode;
			ROB_nxt.Dest=rd;
			ROB_nxt.pc=nowpc;
			
			//RS
			RS_nxt.Op=opcode;
			RS_nxt.Busy=1;
			RS_nxt.A=imm;
			RS_nxt.Reorder=ROB_pos;
			RS_nxt.pc=nowpc;
			
			//Reg
			WQ.rd=rd;WQ.Value=ROB_pos;

		} else if(opcode==0b1101111){//JAL
			rd=x&((1<<5)-1),x>>=5;
			for(int i=12;i<=19;++i)off[i]=x&1,x>>=1;
			off[11]=x&1,x>>=1;
			for(int i=1;i<=10;++i)off[i]=x&1,x>>=1;
			off[20]=x&1,x>>=1;
			imm=get_imm(off,0,20);
			if(RS_las.full())return ;

			//ROB
			unsigned int ROB_pos=ROB_las.nxtpos();
			ROB_nxt.Ready=0;
			ROB_nxt.Inst=opcode;
			if(rd!=0)ROB_nxt.Dest=rd;
			ROB_nxt.pc=nowpc;
			
			//RS
			RS_nxt.Op=opcode;
			RS_nxt.Busy=1;
			RS_nxt.A=imm;
			RS_nxt.Reorder=ROB_pos;
			RS_nxt.pc=nowpc;
			
			//Reg
			if(rd!=0){
				WQ.rd=rd;WQ.Value=ROB_pos;
			}

		} else if(opcode==0b1100111){//JALR
			rd=x&((1<<5)-1),x>>=5;
			func3=x&((1<<3)-1),x>>=3;
			rs1=x&((1<<5)-1),x>>=5;
			for(int i=0;i<=11;++i)off[i]=x&1,x>>=1;
			imm=get_imm(off,0,11);
			if(RS_las.full())return ;

			//ROB
			unsigned int ROB_pos=ROB_las.nxtpos();
			ROB_nxt.Ready=0;
			ROB_nxt.Inst=opcode;
			if(rd!=0)ROB_nxt.Dest=rd;
			ROB_nxt.pc=nowpc;
			
			//RS
			RS_nxt.Op=opcode;
			RS_nxt.Busy=1;
			RS_nxt.A=imm;
			if((int)Reg_las[rs1].Q==-1)RS_nxt.Vj=Reg_las[rs1].V;
			else {
				ROB_node ROB_tmp=ROB_las.take(Reg_las[rs1].Q);
				if(ROB_tmp.Ready)RS_nxt.Vj=ROB_tmp.Value;
				else RS_nxt.Qj=Reg_las[rs1].Q;
			}
			RS_nxt.Reorder=ROB_pos;
			RS_nxt.pc=nowpc;
			
			//Reg
			if(rd!=0){
				WQ.rd=rd;WQ.Value=ROB_pos;
			}

		} else if(opcode==0b1100011){//Branch
			off[11]=x&1,x>>=1;
			for(int i=1;i<=4;++i)off[i]=x&1,x>>=1;
			func3=x&((1<<3)-1),x>>=3;
			rs1=x&((1<<5)-1),x>>=5;
			rs2=x&((1<<5)-1),x>>=5;
			for(int i=5;i<=10;++i)off[i]=x&1,x>>=1;
			off[12]=x&1,x>>=1;
			imm=get_imm(off,0,12);
			if(RS_las.full())return ;

			//ROB
			unsigned int ROB_pos=ROB_las.nxtpos();
			ROB_nxt.Ready=0;
			ROB_nxt.Inst=opcode;
			ROB_nxt.func3=func3;
			ROB_nxt.pc=nowpc;
			
			//RS
			RS_nxt.Op=opcode;
			RS_nxt.Busy=1;
			RS_nxt.A=imm;
			RS_nxt.func3=func3;
			if((int)Reg_las[rs1].Q==-1)RS_nxt.Vj=Reg_las[rs1].V;
			else {
				ROB_node ROB_tmp=ROB_las.take(Reg_las[rs1].Q);
				if(ROB_tmp.Ready)RS_nxt.Vj=ROB_tmp.Value;
				else RS_nxt.Qj=Reg_las[rs1].Q;
			}
			if((int)Reg_las[rs2].Q==-1)RS_nxt.Vk=Reg_las[rs2].V;
			else {
				ROB_node ROB_tmp=ROB_las.take(Reg_las[rs2].Q);
				if(ROB_tmp.Ready)RS_nxt.Vk=ROB_tmp.Value;
				else RS_nxt.Qk=Reg_las[rs2].Q;
			}
			RS_nxt.Reorder=ROB_pos;
			RS_nxt.pc=nowpc;

			// printf("Branch's pc: %d\n",nowpc);

		} else if(opcode==0b0010011){
			rd=x&((1<<5)-1),x>>=5;
			func3=x&((1<<3)-1),x>>=3;
			rs1=x&((1<<5)-1),x>>=5;
			if(rd==0){
				IFqueue.pop();
				return ;
			}
			if(RS_las.full())return ;

			if(func3==0b001||func3==0b101){//SLLI or SRLI or SRAI
				shamt=x&((1<<5)-1),x<<=5;
				func7=x&((1<<7)-1),x<<=7;

				//ROB
				unsigned int ROB_pos=ROB_las.nxtpos();
				ROB_nxt.Ready=0;
				ROB_nxt.Inst=opcode;
				ROB_nxt.Dest=rd;
				ROB_nxt.func3=func3;
				ROB_nxt.func7=func7;
				ROB_nxt.shamt=shamt;
				ROB_nxt.pc=nowpc;
				
				//RS
				RS_nxt.Op=opcode;
				RS_nxt.Busy=1;
				RS_nxt.func3=func3;
				RS_nxt.func7=func7;
				RS_nxt.shamt=shamt;
				if((int)Reg_las[rs1].Q==-1)RS_nxt.Vj=Reg_las[rs1].V;
				else {
					ROB_node ROB_tmp=ROB_las.take(Reg_las[rs1].Q);
					if(ROB_tmp.Ready)RS_nxt.Vj=ROB_tmp.Value;
					else RS_nxt.Qj=Reg_las[rs1].Q;
				}
				RS_nxt.Reorder=ROB_pos;
				RS_nxt.pc=nowpc;

				// printf("issue's info RS's Vj: %d Qj: %d\n",RS_nxt.Vj,RS_nxt.Qj);
				// printf("ROB_las's info Value: %d\n",ROB_las.take(4).Value);
				
				//Reg
				WQ.rd=rd;WQ.Value=ROB_pos;
			} else {
				for(int i=0;i<=11;++i)off[i]=x&1,x>>=1;
				imm=get_imm(off,0,11);

				//ROB
				unsigned int ROB_pos=ROB_las.nxtpos();
				ROB_nxt.Ready=0;
				ROB_nxt.Inst=opcode;
				ROB_nxt.Dest=rd;
				ROB_nxt.func3=func3;
				ROB_nxt.pc=nowpc;
				ROB_nxt.End=isEnd;
				
				//RS
				RS_nxt.Op=opcode;
				RS_nxt.A=imm;
				RS_nxt.Busy=1;
				RS_nxt.func3=func3;
				if((int)Reg_las[rs1].Q==-1)RS_nxt.Vj=Reg_las[rs1].V;
				else {
					ROB_node ROB_tmp=ROB_las.take(Reg_las[rs1].Q);
					if(ROB_tmp.Ready)RS_nxt.Vj=ROB_tmp.Value;
					else RS_nxt.Qj=Reg_las[rs1].Q;
				}
				RS_nxt.Reorder=ROB_pos;
				RS_nxt.pc=nowpc;
				
				//Reg
				WQ.rd=rd;WQ.Value=ROB_pos;
			}
		} else if(opcode==0b0110011){
			rd=x&((1<<5)-1),x>>=5;
			func3=x&((1<<3)-1),x>>=3;
			rs1=x&((1<<5)-1),x>>=5;
			rs2=x&((1<<5)-1),x>>=5;
			func7=x&((1<<7)-1),x>>=7;
			if(rd==0){
				IFqueue.pop();
				return ;
			}
			if(RS_las.full())return ;

			//ROB
			unsigned int ROB_pos=ROB_las.nxtpos();
			ROB_nxt.Ready=0;
			ROB_nxt.Inst=opcode;
			ROB_nxt.Dest=rd;
			ROB_nxt.func3=func3;
			ROB_nxt.func7=func7;
			ROB_nxt.pc=nowpc;
			
			//RS
			RS_nxt.Op=opcode;
			RS_nxt.Busy=1;
			RS_nxt.func3=func3;
			RS_nxt.func7=func7;
			if((int)Reg_las[rs1].Q==-1)RS_nxt.Vj=Reg_las[rs1].V;
			else {
				ROB_node ROB_tmp=ROB_las.take(Reg_las[rs1].Q);
				if(ROB_tmp.Ready)RS_nxt.Vj=ROB_tmp.Value;
				else RS_nxt.Qj=Reg_las[rs1].Q;
			}
			if((int)Reg_las[rs2].Q==-1)RS_nxt.Vk=Reg_las[rs2].V;
			else {
				ROB_node ROB_tmp=ROB_las.take(Reg_las[rs2].Q);
				if(ROB_tmp.Ready)RS_nxt.Vk=ROB_tmp.Value;
				else RS_nxt.Qk=Reg_las[rs2].Q;
			}
			RS_nxt.Reorder=ROB_pos;
			RS_nxt.pc=nowpc;
			
			//Reg
			WQ.rd=rd;WQ.Value=ROB_pos;
		}
	
		IFqueue.pop();
	}

	void EX(){
		CDB_EX.clear();
		if(Exc.Op==0)return ;

		// printf("EX Reorder: %d Op: %d pc: %d func3: %d\n",Exc.Reorder,Exc.Op,Exc.pc,Exc.func3);
		
		if(Exc.Op==0b0110111){//LUI
			CDB_EX.Reorder=Exc.Reorder;
			CDB_EX.Value=Exc.A;
			return ;
		} else if(Exc.Op==0b0010111){//AUIPC
			CDB_EX.Reorder=Exc.Reorder;
			CDB_EX.Value=Exc.A+Exc.pc;
			return ;
		} else if(Exc.Op==0b1101111){//JAL
			CDB_EX.Reorder=Exc.Reorder;
			CDB_EX.Value=Exc.pc+4;
			CDB_EX.Jump=Exc.pc+Exc.A;
			// printf("JAL's Jump: %d Imm: %d\n",CDB_EX.Jump,Exc.A);
			return ;
		} else if(Exc.Op==0b1100111){//JALR
			CDB_EX.Reorder=Exc.Reorder;
			CDB_EX.Value=Exc.pc+4;
			CDB_EX.Jump=(Exc.Vj+Exc.A)&~1;
			// printf("JALR's Jump: %d\n",CDB_EX.Jump);
			return ;
		} else if(Exc.Op==0b1100011){//BEQ ~ BGEU
			if(Exc.func3==0b000){//BEQ
				CDB_EX.Reorder=Exc.Reorder;
				if(Exc.Vj==Exc.Vk)CDB_EX.Jump=Exc.pc+Exc.A;
				return ;
			} else if(Exc.func3==0b001){//BNE
				CDB_EX.Reorder=Exc.Reorder;
				// printf("BNE's x[rs1]: %d x[rs2]: %d\n",Exc.Vj,Exc.Vk);
				if(Exc.Vj!=Exc.Vk)CDB_EX.Jump=Exc.pc+Exc.A;
				return ;
			} else if(Exc.func3==0b100){//BLT
				CDB_EX.Reorder=Exc.Reorder;
				if((int)Exc.Vj<(int)Exc.Vk)CDB_EX.Jump=Exc.pc+Exc.A;
				return ;
			} else if(Exc.func3==0b101){//BGE
				CDB_EX.Reorder=Exc.Reorder;
				if((int)Exc.Vj>=(int)Exc.Vk)CDB_EX.Jump=Exc.pc+Exc.A;
				return ;
			} else if(Exc.func3==0b110){//BLTU
				CDB_EX.Reorder=Exc.Reorder;
				if(Exc.Vj<Exc.Vk)CDB_EX.Jump=Exc.pc+Exc.A;
				// printf("BLTU's x[rs1]: %d x[rs2]: %d Jump: %d\n",Exc.Vj,Exc.Vk,CDB_EX.Jump);
				return ;
			} else if(Exc.func3==0b111){//BGEU
				CDB_EX.Reorder=Exc.Reorder;
				if(Exc.Vj>=Exc.Vk)CDB_EX.Jump=Exc.pc+Exc.A;
				return ;
			}
		} else if(Exc.Op==0b0010011){//ADDI ~ SRAI
			if(Exc.func3==0b000){//ADDI
				CDB_EX.Reorder=Exc.Reorder;
				CDB_EX.Value=Exc.Vj+Exc.A;
				// printf("ADDI's x[rs1]: %d Imm: %d\n",Exc.Vj,Exc.A);
				return ;
			} else if(Exc.func3==0b010){//SLTI
				CDB_EX.Reorder=Exc.Reorder;
				CDB_EX.Value=((int)Exc.Vj<(int)Exc.A)?1:0;
				return ;
			} else if(Exc.func3==0b011){//SLTIU
				CDB_EX.Reorder=Exc.Reorder;
				CDB_EX.Value=(Exc.Vj<Exc.A)?1:0;
				return ;
			} else if(Exc.func3==0b100){//XORI
				CDB_EX.Reorder=Exc.Reorder;
				CDB_EX.Value=Exc.Vj^Exc.A;
				return ;
			} else if(Exc.func3==0b110){//ORI
				CDB_EX.Reorder=Exc.Reorder;
				CDB_EX.Value=Exc.Vj|Exc.A;
				return ;
			} else if(Exc.func3==0b111){//ANDI
				CDB_EX.Reorder=Exc.Reorder;
				CDB_EX.Value=Exc.Vj&Exc.A;
				return ;
			} else if(Exc.func3==0b001){//SLLI
				CDB_EX.Reorder=Exc.Reorder;
				CDB_EX.Value=Exc.Vj<<Exc.shamt;
				// printf("SLLI's x[rs1]: %d shamt: %d Qj: %d\n",Exc.Vj,Exc.shamt,Exc.Qj);
				return ;
			} else if(Exc.func3==0b101){
				if(Exc.func7==0b0000000){//SRLI
					CDB_EX.Reorder=Exc.Reorder;
					CDB_EX.Value=Exc.Vj>>Exc.shamt;
					return ;
				} else if(Exc.func7==0b0100000){//SRAI
					CDB_EX.Reorder=Exc.Reorder;
					unsigned int t=(Exc.Vj&(1<<31))?1:0;
					CDB_EX.Value=0;
					for(int i=1;i<=(int)Exc.shamt;++i)
						CDB_EX.Value|=(t<<(32-i));
					CDB_EX.Value|=Exc.Vj>>Exc.shamt;
					return ;
				}
			}
		} else if(Exc.Op==0b0110011){//ADD ~ AND
			if(Exc.func3==0b000){
				if(Exc.func7==0b0000000){//ADD
					CDB_EX.Reorder=Exc.Reorder;
					CDB_EX.Value=Exc.Vj+Exc.Vk;
					return ;
				} else if(Exc.func7==0b0100000){//SUB
					CDB_EX.Reorder=Exc.Reorder;
					CDB_EX.Value=Exc.Vj-Exc.Vk;
					return ;
				}
			} else if(Exc.func3==0b001){//SLL
				CDB_EX.Reorder=Exc.Reorder;
				CDB_EX.Value=Exc.Vj<<(((1<<5)-1)&Exc.Vk);
				return ;
			} else if(Exc.func3==0b010){//SLT
				CDB_EX.Reorder=Exc.Reorder;
				CDB_EX.Value=((int)Exc.Vj<(int)Exc.Vk)?1:0;
				return ;
			} else if(Exc.func3==0b011){//SLTU
				CDB_EX.Reorder=Exc.Reorder;
				CDB_EX.Value=(Exc.Vj<Exc.Vk)?1:0;
				return ;
			} else if(Exc.func3==0b100){//XOR
				CDB_EX.Reorder=Exc.Reorder;
				CDB_EX.Value=Exc.Vj^Exc.Vk;
				return ;
			} else if(Exc.func3==0b101){
				if(Exc.func7==0b0000000){//SRL
					CDB_EX.Reorder=Exc.Reorder;
					CDB_EX.Value=Exc.Vj>>(((1<<5)-1)&Exc.Vk);
					return ;
				} else if(Exc.func7==0b0100000){//SRA
					CDB_EX.Reorder=Exc.Reorder;
					unsigned int l=((1<<5)-1)&Exc.Vk,t=(Exc.Vj&(1<<31))?1:0;
					CDB_EX.Value=0;
					for(int i=1;i<=(int)l;++i)
						CDB_EX.Value|=(t<<(32-i));
					CDB_EX.Value|=Exc.Vj>>l;
					return ;
				}
			} else if(Exc.func3==0b110){//OR
				CDB_EX.Reorder=Exc.Reorder;
				CDB_EX.Value=Exc.Vj|Exc.Vk;
				return ;
			} else if(Exc.func3==0b111){//AND
				CDB_EX.Reorder=Exc.Reorder;
				CDB_EX.Value=Exc.Vj&Exc.Vk;
				return ;
			}
		}
	}

	void commit(){
		WR.clear();WM.clear();
		Com_res=0;
		if(Com.Inst==0)return ;

		// printf("COM   Dest: %d Val: %d id: %d Inst: %d Jump: %d pc: %d\n",Com.Dest,Com.Value,Com.id,Com.Inst,Com.Jump,Com.pc);
		// for(int i=0;i<SIZE;++i)printf("%d ",Reg_now[i].V);puts("");
		// for(int i=0;i<SIZE;++i)printf("%d ",Reg_now[i].Q);puts("");

		if(Com.Inst==0b1100011){//Branch
			// printf("!Branch: %d\n",Com.Jump);
			if((int)Com.Jump!=-1){
				pc=Com.Jump;
				Com_res=-1;
				return ;
			} else {
				Com_res=1;
				return ;
			}
		} else if(Com.Inst==0b1101111||Com.Inst==0b1100111){//JAL or JALR
			pc=Com.Jump;
			WR.rd=Com.Dest;
			WR.Reorder=Com.id;
			WR.Value=Com.Value;
			Com_res=-1;
			return ;
		} else if(Com.Inst==0b0100011){//Store
//			printf("Commit: %d %d %d\n",Com.id,Com.Inst,Com.pc);
			WM.Reorder=Com.id;
			WM.Value=Com.Value;
			Com_res=1;
			return ;
		} else {//others
			WR.rd=Com.Dest;
			WR.Reorder=Com.id;
			WR.Value=Com.Value;
			Com_res=1;
			// if(WR.rd==13)printf("Reg13's WR Dest: %d Val: %d Reorder: %d\n",WR.rd,WR.Value,WR.Reorder);
			return ;
		}
	}

	void update(){
		RS_las=RS_now;
		ROB_las=ROB_now;
		LSB_las=LSB_now;

		CDB_LSB_las=CDB_LSB_now;
		ROB_LSB_las=ROB_LSB_now;

		for(int i=0;i<SIZE;++i)Reg_las[i]=Reg_now[i];
	}

	void solve(){
		// for(int i=1;i<=500;++i){
		while(true){
			ROB_handle();
			if(Com.End){
				std::cout<<std::dec<<((unsigned int)Reg_las[10].V&255u);
				break;
			}
			LSB_handle();
			RS_handle();
			regfile();
			IF();
			update();

			EX();
			issue();
			commit();

			if((int)Com_res!=-1&&(int)IQ_pc!=-1)IFqueue.push(IQ_pc);
			
			// printf("ClockEnd Reg13's V: %d Q: %d\n\n",Reg_now[13].V,Reg_now[13].Q);
			// puts("");
			// printf("Clock: %d's Regfile\n",i);
			// for(int i=0;i<SIZE;++i)cout<<Reg_now[i].V<<' '<<(int)Reg_now[i].Q<<'\n';puts("");
		}
	}
};

int main(){
	simulator CPU;
	CPU.read();
	CPU.solve();
	return 0;
}