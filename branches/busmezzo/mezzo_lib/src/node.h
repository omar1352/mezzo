/*
	Mezzo Mesoscopic Traffic Simulation 
	Copyright (C) 2008  Wilco Burghout

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * modification:
 *   add bounding box to the node object
 *
 * Xiaoliang Ma 
 * last change: 2007-10-20 
 */

#ifndef NODE_HH
#define NODE_HH


#include "link.h"
#include <vector>
#include <string>
#include "turning.h"
#include "q.h"
#include "vehicle.h"
#include "od.h"
#include "eventlist.h"
#ifndef _NO_GUI
	#include "icons.h"
#endif //_NO_GUI
#ifdef _PVM
	#include "pvm.h"
#endif // _NO_PVM

#ifdef _VISSIMCOM
//#include "vissimcom.h"
#endif _VISSIMCOM


//#define _DEBUG_NODE

using namespace std;

class Turning;
class Link;
class VirtualLink;
class InputLink;
class Q;
class Vehicle;
class Signature;
class ODpair;
class Daction;
class BOaction;
class Busroute;


#ifdef _PVM
class PVM;
#endif //_NO_PVM
typedef pair<Link*,double> linkrate;

class Coord
{
	public:
	Coord();
	Coord(double x_, double y_);
	double x;
	double y;		
};

class Node
{
  public:
	Node(int id_);
	virtual ~Node();
	virtual const int get_id();
	virtual const string className(){return "Node";}
	virtual void reset(); // resets the state variables to 0
	void setxy(double x, double y);
	const Coord getxy();
#ifndef _NO_GUI
	Icon* get_icon(){return icon;}
	void set_icon(NodeIcon* icon_);
#endif // _NO_GUI   
	virtual bool  process_veh(Vehicle* veh, double time);
 protected:
	int id;
	Coord position;
#ifndef _NO_GUI  
	NodeIcon* icon;
#endif // _NO_GUI  
};

class Origin : public Node
{
public:
	Origin (int id_);
	virtual ~Origin();
	virtual void reset(); // resets the state variables to restart

	virtual const string className(){return "Origin";}
	virtual bool transfer_veh(Link* link, double time); // transfers the vehicle from InputQueue to one of the
																				//outgoing links
	virtual bool insert_veh(Vehicle* veh, double time); // inserts the vehicle into the InputQueue
	virtual void add_odpair(ODpair* odpair);
	virtual void register_links(map <int,Link*> linkmap); // registers the outgoing links

	virtual vector<Link*> get_links() {return outgoing;} // returns all outgoing links
	
	virtual void broadcast_incident_start(int lid, vector <double> parameters); // broadcasts incident to all vehicles starting from this origin
	virtual void broadcast_incident_stop(int lid); // stops broadcast of incident

	void write_v_queues(ostream& out);

protected:
	//vector <OServer*> servers;
	InputLink* inputqueue;
	vector <Link*>  outgoing;	
	vector <ODpair*> odpairs;
	bool incident; // flag that indicates incident behaviour
	int incident_link;
	vector <double> incident_parameters;
	vector <int> v_queue_lengths; // stores the length of the virtual queue for each ass_od time period.
	int currentperiod;
};

class Destination : public Node
{
public:
	 Destination (int id_, Server* server_);
	 Destination (int id_);
	 virtual const string className(){return "Destination";}
	 virtual ~Destination();
	 virtual void register_links(map <int,Link*> linkmap);
	 virtual bool execute(Eventlist* eventlist, double time);
protected:
	 vector <Link*>  incoming;
	 vector <Daction*> dactions;
	 Server* server;		
};

class Junction : public Node
{
	public:	
	 Junction (int id_);
	 virtual const string className(){return "Junction";}
	 void register_links(map <int,Link*> linkmap) ;
   	 vector <Link*> get_incoming() {return incoming;}
	 vector <Link*> get_outgoing() {return outgoing;}
	private:
	 vector <Turning*> turnings; // This seems to be unused at the moment!
	 vector <Link*>  incoming;
	 vector <Link*>  outgoing;
};

class BoundaryOut : public Junction
{
 public:
 	BoundaryOut (int id_);
 	~BoundaryOut();
	virtual const string className(){return "BoundaryOut";}
 	void block(int code,double speed); // spread the news to the virtual links (setting full to true/false)
 	void register_virtual(VirtualLink* vl) { vlinks.insert(vlinks.begin(),vl);}
	vector <VirtualLink*> & get_virtual_links() {return vlinks;}
#ifdef _MIME
	vector <Signature*> & get_signatures() ;
	void add_signature(Signature* sig);   
#endif //_MIME

#ifdef _PVM 
	int send_message (PVM* com);   // sends messages. returns nr of sigs that are sent
#endif //_PVM
  
#ifdef _VISSIMCOM
	// put here the equivalent of the above functions

#endif //_VISSIMCOM

	bool blocked() {return blocked_;}
 	bool process_veh(Vehicle* veh, double time);

 protected:
 	vector <VirtualLink*> vlinks;
#ifdef _MIME
 	vector <Signature*> signatures;
#endif //_MIME
	bool blocked_;
} ;

typedef pair <int,int> odval;

class BoundaryIn : public Origin
/* BoundaryIn is a special Origin, in the sense that vehicles that are generated here are generated from a stream (PVM or other)
	that is connected to a microscopic model
*/
{	
	public:
	BoundaryIn (int id_);
	~BoundaryIn ();
	void register_virtual(VirtualLink* vl) { vlinks.insert(vlinks.begin(),vl);}
	//void register_routes(vector<Route*> * routes_){routes=routes_;}
	void register_routes (multimap<odval,Route*> * routemap_) {routemap=routemap_;}
	void register_busroutes (vector<Busroute*> * busroutes_) {busroutes=busroutes_;}
	void register_ods(vector<ODpair*> *ods_){ods=ods_;}
    vector <VirtualLink*> & get_virtual_links() {return vlinks;}
	Route* find_route(odval val, int id);
#ifdef _PVM	 // PVM specific functions
	bool receive_message(PVM* com);
	int send_message (PVM* com, double time);   // sends messages. returns nr of sigs that are sent 
#endif//_PVM
#ifdef _VISSIMCOM
	// do something similar as the PVM case
		double get_speed(double time); // sends the speed assigned to the last vehicle that entered
#endif //_VISSIMCOM


#ifdef _MIME // common functions for both the PVM implementation (Mitsim) and the COM implementation  (VISSIM)
	bool newvehicle(Signature* sig);
#endif // _MIME	

private:
	vector <VirtualLink*> vlinks;
	//vector <Route*> * routes;
	multimap <odval,Route*> * routemap;
	vector <Busroute*> * busroutes; 
	vector <ODpair*> * ods;
	Vehicle* lastveh;
};



class Daction : public Action
{
 public:
 	Daction(Link* link_, Destination* destination_, Server* server_);
 	virtual ~Daction();
 	virtual bool execute(Eventlist* eventlist, double time);
 	virtual bool process_veh(double time);	
 protected:
  Link* link;
  Destination* destination;
  Server* server;
} ;


#endif
