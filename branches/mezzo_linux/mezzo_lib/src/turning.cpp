#include "turning.h"



Turning::Turning(int id_, Node* node_, Server* server_, Link* inlink_, Link* outlink_, int size_):
	id(id_), node(node_), server(server_), inlink(inlink_), outlink(outlink_), size(size_)

	// modified 2003-10-10: multiple turnactions: one for each OUTGOING lane.
  // modified 2004-06-07: one turnaction for MIN (nr_lanes_in, nr_lanes_out).

{
  int nr_servers= _MIN(outlink->get_nr_lanes(),inlink->get_nr_lanes());
   for (int i=0; i < nr_servers; i++)
			turnactions.push_back(new TurnAction(this));
	delay=server->get_delay();
#ifdef _DEBUG_TURNING	
	cout << "new turning: tid,nid,sid,in,out " << id << node->get_id();
	cout << server->get_id() << inlink->get_id() << outlink->get_id() << endl;
#endif //_DEBUG_TURNING
 blocked=false;
 active = true; // turning is active by default
}

Turning::~Turning()
{

 for (vector <TurnAction*>::iterator iter=turnactions.begin();iter<turnactions.end();)
	{
		delete (*iter); // calls automatically destructor
		iter=turnactions.erase(iter);
	}

}

bool Turning::process_veh(double time)
// added 2002-03-11: DELAY for entering the vehicle.
// 2003-11-25: added blocking /unblocking
 /************************ old stuff. clean up later**************
{
#ifdef _DEBUG_TURNING	
	 	cout << " Turning " << id << " from link " << inlink->get_id() <<
	 		" to " << outlink->get_id() << endl;	
#endif //_DEBUG_TURNING	
   ok=false;	
   nexttime=0.0;
	if ( !(outlink->full(time)) && !(inlink->empty()) )
		{      
          Vehicle* veh=inlink->exit_veh(time, outlink, size);
          if (inlink->exit_ok())
          {
			   ok=outlink->enter_veh(veh, time+delay);
               if (is_blocked()) // IF this turning was blocked and becomes unblocked, update the exit times for the vehicles in queue
               {
                 inlink->update_exit_times(time,outlink,size); // update the exit times
                 unblock(); // unblock this turning
                 inlink->remove_blocked_exit(); // tell the link that this turning is not blocked anymore.
               }
			}
          else
				nexttime=inlink->next_action(time);
	   }
  else
  {
      if(outlink->full(time) && !(is_blocked()))
      {
       block(); // block this turning
       inlink->add_blocked_exit(); // tell the link
      }
  }
  
	return ok;
}
*********************************************************/
{
  ok=true;
  out_full=outlink->full(time);
  nexttime=time;
  if (out_full) // if the outlink is full
  {
    if (!blocked) // block if not blocked
    {
      blocked=true;
      inlink->add_blocked_exit(); // tell the link
    }
    nexttime=time+1.0; // next time the turning should try to put something in. Maybe this can be optimized.
    return false;
  }
  else   // outlink is not full
  {
    // unblock if necessary
    if (blocked)
    {
      blocked=false;
      inlink->update_exit_times(time,outlink,size); // update the exit times
      inlink->remove_blocked_exit(); // tell the link that this turning is not blocked anymore.
    }
    if (inlink->empty())
    {
      nexttime=time+(inlink->get_freeflow_time());
      return false;
    }
    else	
	{
		Vehicle* veh=inlink->exit_veh(time, outlink, size);
		if (inlink->exit_ok())
		{
			ok=outlink->enter_veh(veh, time+delay);	
           if (ok)
             return true;
           else
           {
             cout << " turning " << id << " dropped a vehicle, because outlink couldnt enter vehicle" << endl;
             nexttime=inlink->next_action(time);
             delete veh;
             return false;
           }           
        }
       else
        {
			nexttime=inlink->next_action(time);
           if (nexttime <= time)
               cout << "nexttime <= time in Turning::process_veh, from inlink::next_action " << endl;
          return false;
        }
	 }
   }
  return ok;
}
 


double Turning::next_time(double time)
{
	return server->next(time) ;
}

bool Turning::execute(Eventlist* eventlist, double time)
{
  bool ok=true;
  for (vector<TurnAction*>::iterator iter=turnactions.begin(); iter < turnactions.end(); iter++)
  {
   ok=ok && (*iter)->execute(eventlist, time);
  }
  return ok;
}

bool Turning::init(Eventlist* eventlist, double time)
{
  bool ok=true;
  for (vector<TurnAction*>::iterator iter=turnactions.begin(); iter < turnactions.end(); iter++)
  {
   ok=ok && (*iter)->execute(eventlist, time);
   time += 0.00000003; // to make sure time is unique in eventlist
  }
  return ok;
}

void Turning::activate (Eventlist* eventlist,double time)
{
	active = true;
	for (vector<TurnAction*>::iterator iter=turnactions.begin(); iter < turnactions.end(); iter++)
	{
		 (*iter)->execute(eventlist, time);
		 time += 0.00000003; // to make sure time is unique in eventlist
	}
} 

double Turning::link_next_time(double time)
{
	return inlink->next_action(time);
}

TurnAction::TurnAction(Turning* turning_):Action(), turning(turning_) {}

bool TurnAction::execute (Eventlist* eventlist, double time)
{
	
	// process vehicle if any  FOR THIS DIRECTION
	if (turning->is_active())
	{
		bool exec_ok=turning->process_veh(time);
		// get new time from server if exec_ok; otherwise get it from link if applicable.
	   if (exec_ok)
			new_time=turning->next_time(time); // the action went fine, get time for next action
	   else
		{
    		new_time=turning->nexttime; // otherwise the function process_veh has provided a next time.
			if (new_time < time)
			{
				cout << "trouble in the turning action:: exectute. newtime < current time " << endl;
				new_time=time+0.1;
			}
		 }
    
	#ifdef _DEBUG_TURNING	
		cout <<"Turnaction::execute...result: " << exec_ok;
   		cout << " ...next turnaction @ " << new_time << endl;
	#endif //_DEBUG_TURNING
		// book myself in Eventlist with new time;
		eventlist->add_event(new_time,this);
	}
	 return true;
}

void Turning::write(ostream& out)
{
	out << "{ " << id <<" "<< node->get_id()<< " " << server->get_id() << " " << inlink->get_id() << " " << outlink->get_id();
	out << " " << size << " }" << endl;
}

