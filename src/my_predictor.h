// my_predictor.h
// This file contains a sample gshare_predictor class.
// It is a simple 32,768-entry gshare with a history length of 15.

class gshare_update : public branch_update {
public:
	unsigned int index;
};

class gshare_predictor : public branch_predictor {
public:
#define HISTORY_LENGTH	15
#define TABLE_BITS	15
	gshare_update u;
	branch_info bi;
	unsigned int history;
	unsigned char tab[1<<TABLE_BITS];

	gshare_predictor (void) : history(0) { 
		memset (tab, 0, sizeof (tab));
	}

	branch_update *predict (branch_info & b) {
		bi = b;
		if (b.br_flags & BR_CONDITIONAL) {
			u.index = 
				  (history) 
				^ (b.address & ((1<<TABLE_BITS)-1));
			u.direction_prediction (tab[u.index] >> 1);
		} 
        else {
			u.direction_prediction (true);
		}
		u.target_prediction (0);
		return &u;
	}

	void update (branch_update *u, bool taken, unsigned int target) {
		if (bi.br_flags & BR_CONDITIONAL) {
			unsigned char *c = &tab[((gshare_update*)u)->index];
			if (taken) {
				if (*c < 3) (*c)++;
			} else {
				if (*c > 0) (*c)--;
			}
			history <<= 1;
			history |= taken;
			history &= (1<<HISTORY_LENGTH)-1;
		}
	}
};

//
// Pentium M hybrid branch predictors
// This class implements a simple hybrid branch predictor based on the Pentium M branch outcome prediction units. 
// Instead of implementing the complete Pentium M branch outcome predictors, the class below implements a hybrid 
// predictor that combines a bimodal predictor and a global predictor. 
class pm_update : public branch_update {
public:
        unsigned int loc_index;
        unsigned int glo_index; //minus tag
        unsigned int glo_way;
        unsigned int glo_tag;
        bool glo_hit;
};

class pm_predictor : public branch_predictor {
public:
#define LOC_TABLE_BITS	12

#define GLO_HISTORY_LENGTH	14
#define GLO_TABLE_BITS	9
#define GLO_TABLE_WAY 4

    pm_update u;
	branch_info bi;
    
    unsigned char loc_tab[1<<LOC_TABLE_BITS];

	unsigned char glo_tab [1<<GLO_TABLE_BITS][GLO_TABLE_WAY*3 +GLO_TABLE_WAY]; //3colums per way +  lru columns 1 per way
	unsigned int glo_history;
	
        pm_predictor (void)
        {
            glo_history=0;
            memset (loc_tab, 0, sizeof (loc_tab));
            memset (glo_tab , 0, sizeof (glo_tab[0][0])*(1<<GLO_TABLE_BITS)*(GLO_TABLE_WAY*3+GLO_TABLE_WAY));
            for (int i = 0; i < (1<<GLO_TABLE_BITS) ; i++)
            {
                for (int j = 0; j < GLO_TABLE_WAY; j++)
                {
                    glo_tab[i][GLO_TABLE_WAY*3+j] = GLO_TABLE_WAY - 1 - j;
                }
            }
        }

        branch_update *predict (branch_info & b)
        {
            bi = b;

            if (b.br_flags & BR_CONDITIONAL)
            {
                //local prediction start here
                //u.loc_index = ((b.address & ((1<<6)-1))>>2);  // using 4 address bits: 2 through 5
                u.loc_index = (b.address & ((1<<LOC_TABLE_BITS)-1));  // using 12 address bits: 0 through 12
                bool loc_pred = loc_tab[u.loc_index]>>1;

                //global prediction start here
                //unsigned int hash = (glo_history & ((1<<GLO_HISTORY_LENGTH)-1)) 
				//   ^ ((b.address & ((1<<6)-1))>>2);  // using 4 address bits 2 through 5
                unsigned int hash = (glo_history & ((1<<GLO_HISTORY_LENGTH)-1)) 
				   ^ (b.address & ((1<<GLO_HISTORY_LENGTH)-1));  // using 4 address bits: 0 through 4

                u.glo_tag = (hash) ^ ((1<<(GLO_HISTORY_LENGTH - GLO_TABLE_BITS))-1); // tag is the 6 lower bits 0-5
                u.glo_index = (hash >> (GLO_HISTORY_LENGTH - GLO_TABLE_BITS)) ^ ((1<< (GLO_TABLE_BITS))-1); // index is the 0 + 8 upper bits 6-3
                u.glo_hit = false;
                bool glo_pred = false;
                for (int i = 0; i < GLO_TABLE_WAY; i++)
                {
                    bool valid_x = glo_tab[u.glo_index][i*3+0];
                    unsigned char tag_x = glo_tab[u.glo_index][i*3+1];
                    unsigned char pred_x = glo_tab[u.glo_index][i*3+2] >> 1;
                    if (u.glo_tag == (int)tag_x  && valid_x) //TODO use 1 here? for bool
                    {
                        u.glo_hit = true;
                        u.glo_way = i;
                        glo_pred = pred_x;
                        break;
                    }
                }

                //final  prediction start here
                if (u.glo_hit == true)
                     u.direction_prediction(glo_pred);
                else
                     u.direction_prediction(loc_pred);
            }
            else
            {
                u.direction_prediction (true);
            }
            
			// predict branch target address
            u.target_prediction (0);
            
			return &u;
        }

        void update (branch_update *u, bool taken, unsigned int target)
        {
            if (bi.br_flags & BR_CONDITIONAL)
            {
                //local update start here
                unsigned char *loc_c = &loc_tab[((pm_update*)u)->loc_index];
                if (taken) {
                    if (*loc_c <3) (*loc_c)++;
                } else {
                    if ( *loc_c >0) (*loc_c)--;
                }

                //global update start here
                unsigned int glo_idxx = ((pm_update*)u)->glo_index;
                unsigned int glo_tagg = ((pm_update*)u)->glo_tag;
                unsigned int glo_wayy = ((pm_update*)u)->glo_way;
                bool glo_hitt = ((pm_update*)u)->glo_hit;
                if (glo_hitt)
                {
                    //if there was a hit, update existing entry
                    //get the pointer to correct counter on correct way glob table
                    unsigned char *glo_c = &glo_tab[glo_idxx][glo_wayy * 3 + 2]; 
                    if (taken) {
                        if (*glo_c <3) (*glo_c)++;
                    } else {
                        if (*glo_c >0) (*glo_c)--;
                    }

                    //update lru
                    // check if way already in LRU
                    int way_position_in_LRU = 9;
                    for (int j = 0; j < GLO_TABLE_WAY; j++)
                    {
                        if (glo_tab[glo_idxx][GLO_TABLE_WAY*3+j]== glo_wayy)
                        {
                            way_position_in_LRU = j;
                            break;
                        }
                    }
                    if (way_position_in_LRU!=9)//should always be true
                    {
                        for (int k = way_position_in_LRU-1; k >= 0; k--)
                        {
                            glo_tab[glo_idxx][GLO_TABLE_WAY*3+k+1] = glo_tab[glo_idxx][GLO_TABLE_WAY*3+k];
                        }
                        glo_tab[glo_idxx][GLO_TABLE_WAY*3]=glo_wayy;
                    }

                }
                else
                {
                    //if no hit, put new entry

                    //1. find the LRU way 
                    unsigned char lru_way = glo_tab[glo_idxx][GLO_TABLE_WAY*3+GLO_TABLE_WAY-1];
                    
                    //set the valid, tag, counter on LRU way
                    glo_tab[glo_idxx][lru_way*3+0] = 1; //validd
                    glo_tab[glo_idxx][lru_way*3+1] = glo_tagg; //tagg
                    glo_tab[glo_idxx][lru_way*3+2] =0; // counter

                    //set new LRU way
                    for (int k = GLO_TABLE_WAY-2; k >= 0; k--)
                    {
                        glo_tab[glo_idxx][GLO_TABLE_WAY*3+k+1] = glo_tab[glo_idxx][GLO_TABLE_WAY*3+k];
                    }
                    glo_tab[glo_idxx][GLO_TABLE_WAY*3]=lru_way;
                }

                //adjust global history here GHR (4 bit)
                glo_history <<= 1;
                glo_history |= taken;
                glo_history &= (1<<GLO_HISTORY_LENGTH)-1;
            }
        }

};

//
// Complete Pentium M branch predictors for extra credit
// This class implements the complete Pentium M branch prediction units. 
// It implements both branch target prediction and branch outcome predicton. 
class cpm_update : public branch_update {
public:
        unsigned int index;
};

class cpm_predictor : public branch_predictor {
public:
        cpm_update u;

        cpm_predictor (void) {
        }

        branch_update *predict (branch_info & b) {
            u.direction_prediction (true);
            u.target_prediction (0);
            return &u;
        }

        void update (branch_update *u, bool taken, unsigned int target) {
        }

};


