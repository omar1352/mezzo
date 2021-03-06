
#include "link.h"
#include <list>
#include <string>
#include "grid.h"

Link::Link(int id_, Node* in_, Node* out_, int length_, int nr_lanes_, Sdfunc* sdfunc_): id(id_), in_node (in_),
		out_node(out_), length(length_), nr_lanes(nr_lanes_), sdfunc(sdfunc_)
{
//	maxcap=static_cast <int> (sdfunc->get_romax()*length*nr_lanes/1000);
	maxcap=static_cast<int> (length*nr_lanes/theParameters->standard_veh_length);
//  maxcap=length*nr_lanes;      // now maxcap is the total space that is available for vehicles
#ifdef _DEBUG_LINK	
	cout << "link " << id << " maxcap " << maxcap << endl;
#endif //_DEBUG_LINK
	
#ifdef _COLLECT_TRAVELTIMES
	const int nr_fields=3;
	string names[nr_fields]={"lid","time","traveltime"};
    vector <string> fields(names,names+nr_fields);
	grid=new Grid(nr_fields,fields);
#endif // _COLLECT_TRAVELTIMES
#ifdef _COLLECT_ALL
	const int nr_fields=8;
	string names[nr_fields]= {"time","vid","lid","entry?","density","qlength","speed","earliest_exit_time"};
   vector <string> fields(names,names+nr_fields);
	grid=new Grid(nr_fields,fields);
#endif //_COLLECT_ALL	
	avg_time=0.0;
	avgtimes=new LinkTime();
   histtimes=NULL;
  nr_passed=0;
	running_percentage=0.0;
	queue_percentage=0.0;
	blocked=false;
	moe_speed=new MOE(theParameters->moe_speed_update, 3.6);
	moe_inflow=new MOE(theParameters->moe_inflow_update, (3600.0/theParameters->moe_inflow_update));
	moe_outflow=new MOE(theParameters->moe_outflow_update, (3600.0/theParameters->moe_outflow_update));

//	moe_inflow=new MOE(theParameters->moe_inflow_update, ((3600.0/theParameters->moe_inflow_update)/nr_lanes));
//	moe_outflow=new MOE(theParameters->moe_outflow_update, ((3600.0/theParameters->moe_outflow_update)/nr_lanes));
//	moe_inflow=new MOE(theParameters->moe_inflow_update);
//	moe_outflow=new MOE(theParameters->moe_outflow_update);
	
	moe_queue=new MOE(theParameters->moe_queue_update);
	moe_density=new MOE(theParameters->moe_density_update);
  blocked_until=-1.0; // -1.0 = not blocked, -2.0 = blocked until further notice, other value= blocked until value
  nr_exits_blocked=0; // set by the turning movements if they are blocked
  freeflowtime=(length/(sdfunc->speed(0.0)));
  if (freeflowtime < 1.0)
      freeflowtime=1.0;
  queue=new Q(maxcap, freeflowtime);    
  use_ass_matrix = false;
  selected = false;
}


Link::Link()
{
	moe_speed=new MOE(theParameters->moe_speed_update);
	moe_inflow=new MOE(theParameters->moe_inflow_update);
	moe_outflow=new MOE(theParameters->moe_outflow_update);
	moe_queue=new MOE(theParameters->moe_queue_update);
	moe_density=new MOE(theParameters->moe_density_update);
  blocked_until=-1.0; // -1.0 = not blocked, -2.0 = blocked until further notice, other value= blocked until value
  nr_exits_blocked=0; // set by the turning movements if they are blocked
  freeflowtime=1.0;	
  selected = false;
}		

Link::~Link()
{
	delete(queue);
	delete(moe_speed);
	delete(moe_outflow);
	delete(moe_inflow);
	delete(moe_queue);
	delete(moe_density);
#ifdef _COLLECT_TRAVELTIMES
	if (grid!=null )
		delete(grid);
#endif
 #ifdef _COLLECT_ALL
	if (grid!=null )
		delete(grid);
#endif
	if (histtimes!=NULL)
		delete (histtimes);
    if (avgtimes!=NULL)
      delete (avgtimes);
	
}


const int Link::get_out_node_id ()
	{return out_node->get_id();}
	
const int Link::get_in_node_id()
	{return in_node->get_id();}

const bool Link::full()
	{
    return queue->full();
  }

  
const bool Link::full(double time)

{
  moe_density->report_value(density(),time); // to make sure even values are reported when queue is full
   if (blocked_until==-2.0)
       return true;

   if (queue->full())
   {    
     if (nr_exits_blocked >0)     // IF one of the turning movements is blocked
         blocked_until=-2.0; // blocked until further notice.
         //cout <<" on link "<< id << " the queue has become full and density = " << density() << " and space occupied ";
         //cout << queue->calc_total_space() << " and total space available: " <<length*nr_lanes << endl;
         
     return true; // IF the queue is full this returns true. IF ALSO at least one exit is blocked, blocked_until=-2.0 and this will stay blocked untill a shockwave.
   }
   
   if (blocked_until==-1.0)
     return false; // no queue
   else  
   {
      if (blocked_until <= time) // queue front has reached upstream node
      {
         blocked_until=-1.0;  // unblock the link
         return false;
      }
      else
         return true; // queue is dissipating, but shockwave front has not reached upstream node yet.
   }   
}
     
const bool Link::empty()
	{return queue->empty();}		

const double Link::next_action (double time)
	{ double newtime;
      if (queue->empty() )
			newtime= (time+freeflowtime);
		else
			newtime= queue->next();
      if (newtime <= time)
        cout << "Link::nextaction: newtime= " << newtime << " and time = " << time <<  " and freeflowtime = " << freeflowtime << endl;
      return newtime;
	}	 // optimisation: return freeflow time if queue is empty

const int Link::size()
	{return queue->size();}	
	
	
void Link::add_alternative(int dest, vector<Link*> route)
	{queue->add_alternative(dest, route);}	

void Link::set_selected (const bool sel) 
{	selected = sel;
#ifndef _NO_GUI	
	icon->set_selected(selected);
#endif // _NO_GUI	
}

void Link::update_exit_times(double time,Link* nextlink, int lookback)
{
   // Not good enough (2004-01-26) need to use the upstream density to calc exit speed & shockwave speeds
   double kjam=142.0;
   double v_exit, v_shockwave, v_dnstr;
   double k_dnstr= nextlink->density();
   v_exit= sdfunc->speed(kjam);   
   v_dnstr=nextlink->speed_density(k_dnstr);    // downstream speeds can be ridiculously low in case Mitsim provides them
                                                                                 // for instance when a 'gap' in the queue crosses the border (the queue
                                                                           // compresses
    if (v_dnstr <= v_exit )                                                                             
           v_dnstr=v_exit-1.0;;
   // v_shockwave=deltaQ/deltaK
   if (k_dnstr >= sdfunc->get_romax())           // just another test
       v_shockwave=v_exit-1.0;
   else
       v_shockwave= (k_dnstr*v_dnstr)/(kjam-k_dnstr); //re-ordered to give a positive value (*-1)

   if (v_shockwave >= v_exit) // due to the limited domain of the speed/density functions we need to check
       v_shockwave=v_exit - 1.0;
   
   /*
    v_exit=sdfunc->speed(k_dnstr);
    v_dnstr=nextlink->speed_density(k_dnstr);
    if (v_dnstr <= v_exit)
    {
           // v_dnstr=v_exit;
           blocked_until=-1.0;

           return;
     }      
   if (k_dnstr >= sdfunc->get_romax())
   {
          // k_dnstr is outside the domain of the speed-density funciton
         
         
          v_shockwave=v_dnstr-0.5;
   }
    
   if (k_dnstr>=kjam)
  {
      cout << " Big trouble!  upstream density >= downstream in queue dissipation..." << endl;
      blocked_until=-1.0;
      return; // no updating of times
    }
    
   else
   {
        v_shockwave= v_dnstr* (k_dnstr/(kjam-k_dnstr)) ;           
   }
    */
#ifdef _DEBUG_LINK
    cout << " VEXIT = " << v_exit << endl;
    cout << " VSHOCKWAVE = " << v_shockwave << endl;
#endif //_DEBUG_LINK
	//double v_shockwave=((kjam*vjam*3.6 - k_dnstr*v_exit*3.6)/(kjam-k_dnstr))/3.6;
   //double v_shockwave=5.0;
   //if (v_shockwave < 0.0)
  //        v_shockwave= - v_shockwave; // can't remember when it's pos or neg...
   queue->update_exit_times(time,nextlink,lookback,v_exit, v_shockwave);
   if (blocked_until==-2.0) // if the link is completely blocked until further notice
   {
          blocked_until=(length/v_shockwave)+time; // time when shockwave reaches the upstream node
#ifdef _DEBUG_LINK
          cout << "shockwave at link " << id << ", blocked until " << blocked_until <<". Time now " << time ;
          cout << ", v_exit = " << v_exit << ", v_shockwave = " << v_shockwave << ", kdownstr = " << k_dnstr << endl;
#endif // _DEBUG_LINK
   }
}   // exit speed is based on downstream density
                                    
  
double Link::speed(double time)
{	
	double ro=0.0;
#ifdef _RUNNING     // if running segment is seperate density is calculated on that part only
	ro=density_running(time);
#else
	#ifdef _RUNNING_ONLY
      ro=density_running_only(time);
	#else	
		ro=density();
	#endif	// _RUNNING_ONLY
#endif // _RUNNING
//	cout << "Link::speed: ro_runningonly = " << ro << " speed is " << sdfunc->speed(ro) ;
//	cout << " ro normal is " << density() <<" nr running " << queue->nr_running(time) <<  endl;
//	cout << " the ro for density_running " << density_running(time) << endl;
	
	return (sdfunc->speed(ro));	
}


double Link::speed_density(double density_)
{
    return sdfunc->speed(density_);
}
bool Link::enter_veh(Vehicle* veh, double time)
{
   double ro=0.0;
#ifdef _RUNNING     // if running segment is seperate density is calculated on that part only
	ro=density_running(time);
#else
	#ifdef _RUNNING_ONLY
      ro=density_running_only(time);
	#else	
		ro=density();
	#endif	//_RUNNING_ONLY
#endif  //_RUNNING
	double speed=sdfunc->speed(ro);	
	//moe_speed->report_value(speed,time);
	moe_density->report_value(density(),time);
	moe_queue->report_value((queue->queue(time)),time);
	moe_inflow->report_value(time);
	double exit_time=(time+(length/speed)) ;
  #ifdef _USE_EXPECTED_DELAY
    double exp_delay=0.0;
    exp_delay=1.44*(queue->queue(time)) / nr_lanes;
    /*
    double current_outflow=nr_lanes*(moe_outflow->get_last_value());
    if (current_outflow > 0.0)          // if < 0.0 then there is no value for outflow, so no queue delay either
      exp_delay=3600.0*(queue->queue(time))/current_outflow;  */
    exit_time=exit_time+exp_delay;
    cout << "link_enter:: exp_delay = " << exp_delay << endl;
    #endif //_USE_EXPECTED_DELAY
   veh->set_exit_time(exit_time);
   veh->set_curr_link(this);
   veh->set_entry_time(time);
   update_icon(time);	
#ifdef _COLLECT_ALL	
	list <double> collector;	
	collector.push_back(time);
	collector.push_back(veh->get_id());
	collector.push_back(id);
	collector.push_back(1.0);
	collector.push_back(ro);
	collector.push_back((queue->size()+1));
	collector.push_back(speed);
	collector.push_back(exit_time);
	grid->insert_row(collector);
#endif //_COLLECT_ALL
#ifdef _DEBUG_LINK
		cout <<"Link::enter_veh lid:  " << id;
		cout <<" time: " << time;
		cout <<" density: " << ro ;
       cout << "  speed:  "<< speed << " m/s";
		cout << "  exit_time: " << exit_time;
	   	cout << " queue size: " << (queue->size()+1) << endl;
#endif //_DEBUG_LINK	
	// report the ass_matrix contribution
	if (use_ass_matrix)
	{
		int cur_l_period = static_cast <int> (time / theParameters->ass_link_period);
		int od_period = static_cast <int> (veh->get_start_time() / theParameters->ass_od_period);
		const odval& od_pair = veh->get_odids();
		// Test first if the location has been used yet, since we only allocate used places in memory
		if ( (ass_matrix[cur_l_period].empty()) || (ass_matrix[cur_l_period] [od_pair].empty()) ||
			 (ass_matrix[cur_l_period] [od_pair].find(od_period) == ass_matrix[cur_l_period] [od_pair].end()))
		{
			ass_matrix [cur_l_period] [od_pair] [od_period] = 1;
		}
		else
		{
			ass_matrix [cur_l_period] [od_pair] [od_period] ++;
		}
	}
	return queue->enter_veh(veh);
}

Vehicle* Link::exit_veh(double time, Link* nextlink, int lookback)
{
	ok=false;
	if (!empty())
	{
		Vehicle* veh=queue->exit_veh(time, nextlink, lookback);
		if (queue->exit_ok())
		{
			double entrytime=veh->get_entry_time();
			double traveltime=(time-entrytime);
			avg_time=(nr_passed*avg_time + traveltime)/(nr_passed+1); // update of the average
			if ((curr_period+1)*(avgtimes->periodlength) < entrytime )
			{			 	
			 	
			 	avgtimes->times.push_back(tmp_avg);
			 	curr_period++;
			 	tmp_avg=0.0;
			 	tmp_passed=0;

			}
			else
			{
				tmp_avg=(tmp_passed*tmp_avg + traveltime)/(tmp_passed+1); // update of the average			
				tmp_passed++;
			}
			nr_passed++;
          list <double> collector;
#ifdef _COLLECT_ALL
			double speed=sdfunc->speed(density());
          double exit_time=veh->get_exit_time();				
			collector.push_back(time);
			collector.push_back(id);
			collector.push_back(0.0);
			collector.push_back(density());
			collector.push_back((queue->size()));
			collector.push_back(speed);	
      		collector.push_back(exit_time);
      		grid->insert_row(collector);
#else
	#ifdef _COLLECT_TRAVELTIMES
			collector.push_back(id);
			collector.push_back(time);			
			collector.push_back(traveltime);			
      		grid->insert_row(collector);
	#endif // _COLLECT_TRAVELTIMES
#endif // _COLLECT_ALL     		
	      update_icon(time);
	      ok=true;
	      veh->add_meters(length);
	      moe_outflow->report_value(time);
	      moe_speed->report_value((length/traveltime),time);
			return veh;
		}

	}
  else
  	return NULL;
  return NULL; 
}



Vehicle* Link::exit_veh(double time)
{
	ok=false;
	if (!empty())
	{
		Vehicle* veh=queue->exit_veh(time);
		if (queue->exit_ok())
		{
			double entrytime=veh->get_entry_time();
			double traveltime=(time-entrytime);
			avg_time=(nr_passed*avg_time + traveltime)/(nr_passed+1); // update of the average
			if ((curr_period+1)*(avgtimes->periodlength) < entrytime )
			{			 	
			 	avgtimes->times.push_back(tmp_avg);
			 	curr_period++;
			 	tmp_avg=0.0;

			 	tmp_passed=0;
			}
			else
			{

				tmp_avg=(tmp_passed*tmp_avg + traveltime)/(tmp_passed+1); // update of the average			
				tmp_passed++;
			}			
			nr_passed++;
			list <double> collector;
#ifdef _COLLECT_ALL
			collector.push_back(time);
			collector.push_back(id);
			collector.push_back(0.0);
			collector.push_back(density());
			collector.push_back((queue->size()));
			double speed=sdfunc->speed(density());
 	   		collector.push_back(speed);
	   		double exit_time=veh->get_exit_time();
      		collector.push_back(exit_time);
	  		grid->insert_row(collector);
#else
	#ifdef _COLLECT_TRAVELTIMES
			collector.push_back(id);
			collector.push_back(time);
			
			collector.push_back(traveltime);
	   		grid->insert_row(collector);  		
	#endif //_COLLECT_TRAVELTIMES
#endif //_COLLECT_ALL	
	   		update_icon(time); // update the icon's queue length
	   		ok=true;
	   		veh->add_meters(length);
	   		 moe_outflow->report_value(time);
	   		 moe_speed->report_value((length/traveltime),time);
			return veh;
		}
	}
	else
		return NULL;
	return NULL;
}

const double Link::density()
{
   int qsize=queue->size() ;
   	return (qsize/(nr_lanes*(length/1000.0))); // veh/km/lane and length is in meters	
}

const double Link::density_running(double time)
{
 	int nr_running=queue->nr_running(time);
   return (nr_running/(nr_lanes*(length/1000.0))); // veh/km and length is in meters
}

// 2002-07-25: new: queue space calculated using individual vehicles lengths
const double Link::density_running_only(double time)
{
  int nr_running=queue->nr_running(time);// nr cars in running segment
 // double queue_space=(queue->size()-nr_running)*theParameters->standard_veh_length; // length of queue
  double queue_space=queue->calc_space(time)/nr_lanes; // 2003_11_29: corrected : div by nr_lanes!
  double running_length=length-queue_space;
 
  	#ifdef _DEBUG_LINK
   	 // cout << "density_running_only:: nr_running " << nr_running;
   	  cout << " queue_space " << queue_space << endl;
   	#endif
    if (nr_running && running_length)
   	{
   	  return (nr_running / (running_length*(nr_lanes/1000.0) ) );
	}
	else
		return 0;
}


bool Link::write(ostream&)      // unused ostream& out
{
#ifndef _COLLECT_ALL
#ifndef _COLLECT_TRAVELTIMES
	return false;
#else
	return grid->write_empty(out);
#endif //_COLLECT_TRAVELTIMES
#endif //_COLLECT_ALL
	
}	


void Link::write_ass_matrix (ostream & out, int linkflowperiod)
{
	if (use_ass_matrix)
	{
		
		int no_entries=0;
		out << "link_id: " << id << endl;
	//	double factor=3600/theParameters->ass_link_period;
		map <int,int> ::iterator ass_iter2;
		ass_iter = ass_matrix [linkflowperiod].begin();
		// calculate the number of non-zero entries
		
		for (unsigned int h=0; h<ass_matrix[linkflowperiod].size(); ++h)
		{
			no_entries += ass_iter->second.size();
			ass_iter++;
		}
		
		out << "no_entries: " << no_entries << endl;
		
		// and reset ass_iter to start writing the entries
		ass_iter = ass_matrix [linkflowperiod].begin();
		for (unsigned int i=0; i<ass_matrix[linkflowperiod].size(); ++i)
		{
			ass_iter2=ass_iter->second.begin();
			for (unsigned int j=0; j<ass_iter->second.size(); ++j)
			{
				out << "{\t" << ass_iter->first.first << "\t" << ass_iter->first.second << "\t" 
					<< ass_iter2->first << "\t" << (ass_iter2->second)/*factor*/ << "\t}" << endl;
				ass_iter2++;
			}
			ass_iter++;
		}
	}
}



void Link::write_time(ostream& out)
{
//#ifdef _COLLECT_TRAVELTIMES
	double newtime=0.0;
	out << "{\t" << id ;
/* ************ Old crap
#ifdef _AVERAGING	
	newtime=(alpha*avg_time)+(1-alpha)*hist_time;
#endif	
 ***************/
  // TEST IF THERE ARE ANY AVG TIMES,    
  if (avgtimes->times.size()==0) // to make sure the old times are rewritten, in case there are no avg times
  {
    if (histtimes) // test if the histtimes exist (they don't if there was a problem reading the file)
      {
          for (vector<double>::iterator iter=(histtimes->times).begin();iter<(histtimes->times).end();iter++)
          {
            newtime=(*iter);
            out << "\t"<< newtime;
          }
        }	
  }
  else
  {
    if (avgtimes->nrperiods > (int)avgtimes->times.size()) // if there are more periods than data in the times vector
    {
          for (int j=0; j < ((avgtimes->nrperiods) -  (int)(avgtimes->times.size())); j++)
              avgtimes->times.push_back(avgtimes->times.back()); // copy the data from the last known position
    }
      //	for (vector<double>::iterator iter=(avgtimes->times).begin();iter<(avgtimes->times).end();iter++)
      for (int period=0;period < (avgtimes->nrperiods); period++)
      {
        double avgtime= 0.0;
		if (unsigned(avgtimes->nrperiods)> avgtimes->times.size())
		{

		}
		else
		{
			avgtime= avgtimes->times[period];
			newtime=(time_alpha)*avgtime+(1-time_alpha)*(histtimes->times[period]);
		}
		if (avgtime == 0) 
		{
			if (histtimes->times[period] > 0 )
				newtime = histtimes->times [period];
			else
				newtime = 1.0;
		}
        //newtime=(*iter);
		if (newtime < 1.0)
			newtime = 1.0;
        out << "\t"<< newtime;
      }
  }
  out << "\t}" << endl;
//#endif _COLLECT_TRAVELTIMES	
}

void Link::update_icon(double time)
{
	double cap;
	cap=maxcap;
	double totalsize, runningsize, queuesize;
	totalsize=queue->size();
	runningsize=queue->nr_running(time);
	queuesize=totalsize-runningsize;
	queue_percentage=(queuesize/cap);
	running_percentage=(runningsize/cap);
#ifdef _DEBUG_LINK	
	cout << " runningsize " << runningsize;
	cout << "  queueingsize " << queuesize << endl;
#endif //_DEBUG_LINK	
}

void Link::set_incident(Sdfunc* sdptr, bool blocked_)
{
	temp_sdfunc=sdfunc;
	sdfunc=sdptr;
	blocked=blocked_;
}

void Link::unset_incident()
{
	sdfunc=temp_sdfunc;
	blocked=false;
	cout << "the incident on link " << id << " is over. " << endl;
}


void Link::broadcast_incident_start(int lid, vector <double> parameters)
{
	queue->broadcast_incident_start(lid, parameters);
}



void Link::receive_broadcast(Vehicle* veh, int lid, vector <double> parameters)

{queue->receive_broadcast(veh,lid,parameters);}



double Link::calc_diff_input_output_linktimes ()
 {
	if (avgtimes->times.size() > 0)
		{
		double total = 0.0;
		vector <double>::iterator newtime=avgtimes->times.begin();
		vector <double>::iterator oldtime=histtimes->times.begin();
		
		for (newtime, oldtime; (newtime < avgtimes->times.end()) && (oldtime < histtimes->times.end()); newtime++, oldtime++)
		{
			if ((newtime < avgtimes->times.end()) && (oldtime < histtimes->times.end()))
			{
				total+= (*newtime) - (*oldtime);
			}
		}
		return total;
	}
	else
		return 0.0;
 }

double Link::calc_sumsq_input_output_linktimes ()
 {
	if (avgtimes->times.size() > 0)
	{
		double total = 0.0;
		vector <double>::iterator newtime=avgtimes->times.begin();
		vector <double>::iterator oldtime=histtimes->times.begin();
		for (newtime, oldtime; (newtime < avgtimes->times.end()) && (oldtime < histtimes->times.end()); newtime++, oldtime++)
		{
			if ((newtime < avgtimes->times.end()) && (oldtime < histtimes->times.end()))
			{
				total+= ((*newtime) - (*oldtime)) * ((*newtime) - (*oldtime)) ;
		
			}
		}
		return total;
	}
	else return 0.0;
}


// InputLink functions
InputLink::InputLink(int id_, Origin* out_)
{
    id=id_;
	out_node=out_;
	maxcap=65535; //
	length=0;
    nr_lanes=0;
    queue=new Q(maxcap, 1.0);
    histtimes=NULL;
    avgtimes=NULL;
}

InputLink::~InputLink()
{
	//delete queue;
}

bool InputLink::enter_veh(Vehicle* veh, double time)
{
  veh->set_exit_time(time);
  veh->set_curr_link(this);
  veh->set_entry_time(time);
  return queue->enter_veh(veh);
}

Vehicle* InputLink::exit_veh(double time, Link* link, int lookback)
{
	ok=false;
	if (!empty())
	{
		Vehicle* veh=queue->exit_veh(time, link, lookback);
		if (queue->exit_ok())
		{
          ok=true;
			return veh;
		}
	}
    return NULL;
}


// Virtual Link functions

VirtualLink::~VirtualLink()

{
//	this->Link::~Link();
/*
	delete(queue);
#ifdef _COLLECT_TRAVELTIMES
	if (grid!=null )
		delete(grid);
#endif
 #ifdef _COLLECT_ALL
	if (grid!=null )
		delete(grid);
#endif
	if (histtimes!=NULL)
		delete (histtimes);*/
}

 VirtualLink::VirtualLink(int id_, Node* in_, Node* out_, int length_, int nr_lanes_, Sdfunc* sdfunc_) : Link(id_,in_,out_,length_,nr_lanes_,sdfunc_)
 {
   blocked=false;
   linkdensity=0.0;
   linkspeed=this->sdfunc->speed(linkdensity);
   freeflowtime=(length/(sdfunc->speed(0.0)));
   if (freeflowtime < 1.0)
      freeflowtime=1.0;
#ifdef _VISSIMCOM
   pathid=id;
#endif //_VISSIMCOM
 }

const  bool VirtualLink::full()
 {
  	return blocked;
 }

 const bool VirtualLink::full(double ) // double time unused
 {
   return blocked;
 }

 double VirtualLink::speed_density(double)    //unused double density
{
    return linkspeed;
}
 
 bool VirtualLink::enter_veh(Vehicle* veh, double time)
 {
  in_headways.push_back(time);
  return in_node->process_veh(veh,time);
  
 }

 bool VirtualLink::exit_veh(Vehicle* veh, double time)
 {
			double entrytime=veh->get_entry_time();
			double traveltime=(time-entrytime);
			//cout << "virtuallink::exit_veh: entrytime: " << entrytime << " exittime " << time << endl;
			avg_time=(nr_passed*avg_time + traveltime)/(nr_passed+1); // update of the average
			if ((curr_period+1)*(avgtimes->periodlength) < entrytime )
			{			 	
			 	avgtimes->times.push_back(tmp_avg);
			 	curr_period++;
			 	tmp_avg=0.0;
			 	tmp_passed=0;
			}
			else
			{
				tmp_avg=(tmp_passed*tmp_avg + traveltime)/(tmp_passed+1); // update of the average			
				tmp_passed++;
			}
       out_headways.push_back(time);
			return true;	

  }


  void VirtualLink::write_in_headways(ostream & out)
  {
   for (list <double>::iterator iter=in_headways.begin(); iter != in_headways.end(); iter++)
   {
     out << (*iter) << endl;
   }
  }

void VirtualLink::write_out_headways(ostream & out)
  {
   for (list <double>::iterator iter=out_headways.begin(); iter != out_headways.end(); iter++)
   {
     out << (*iter) << endl;

   }
  }
