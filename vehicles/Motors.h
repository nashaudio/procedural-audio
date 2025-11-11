#include <klang.h>
using namespace klang::optimised;

// "A Toy Boat Engine"
// from Designing Sound (Farnell, 2010, p511).

static signal clip_0_1(signal in){
	return in < 0.f ? signal(0) : in > 1.f ? signal(1) : in;
}

struct Partial {
	Frequency frequency;
	dB gain;
};

static const Partial partials[15] = {
	{86.1f, 44.5f},  {64.6f, 43.8f},   {43.1f, 40.3f},  {53.8f, 37.0f}, 
	{99.6f, 35.9f},  {21.5f, 35.4f},   {110.4f, 33.8f}, {75.4f, 31.5f}, 
	{175.0f, 29.0f}, {118.4f, 28.5f},  {131.9f, 26.1f}, {142.7f, 24.8f}, 
	{166.9f, 18.7f}, {8.1f, 18.4f},    {185.7f, 17.7f}
};




struct Mini : Sound {

	struct Engine : Generator {
		static constexpr int N = 4;
    
		Sine osc[15];
		Amplitude osc_gain[15];
    
		Noise noise;
		Bank<BPF, N> eq;
		Amplitude eq_gain[N];
		Amplitude shelf;
    
		Delay<512> comb;
    
		LPF lpf;
		OnePole::LPF throttle_lpf;
    
		param rpm = 0, throttle = 0, ignition = 0;
		signal rate = 0, gas = 0, power = 0;
		param gear = 0;

		Envelope starter, rev;

		Engine() {
    
			// engine noise character
			const Frequency f[N] = { 65,   1672, 3316, 9717 };
			const param q[N] =     { 3,    3,    6,    6    };
			const dB g[N] =        { 14.2, 10.3, 6.9,  1.1  };
			shelf = dB(-25) -> Amplitude; // -25dB noise shelf
			for (int i = 0; i < N; ++i) {
				eq[i].set(f[i], q[i]);
				eq_gain[i] = g[i] -> Amplitude;
			}

			// engine tone weights
			for (int p = 0; p < 15; p++) 
        		osc_gain[p] = dB(partials[p].gain - 48) -> Amplitude;

			starter = { { 0, 0 } };
 		}
    
		virtual ~Engine() {}

		// soft clip distortion (for exhaust noise)
		static signal softclip(signal x, signal threshold = 1.5f, signal slope = 1.5f) {
			return threshold * FMath::Tanh(x * slope / threshold);
		}
    
		// configure engine (based on UE Chaos Vehicle)
		void set(param _ignition, param _rpm, param _throttle, param _gear){

			// start engine
			if (_ignition > ignition) {
				starter.initialise();	// restart starter envelope
				rev.initialise();		// restart rev envelope
				throttle_lpf.set(0.05);	// slow throttle response for startup

				// delayed start, followed by revs at ignition
				const param delay = random(0.25, 0.75);
				starter = { {0, .5 },                   { delay, 1 },                                     { delay + .25, 2 }, { delay + .5, 1 } };
				rev = {     {0, 0  }, { delay - .1, 0}, { delay, 1 },       { delay + .125, random(2,5)}, { delay + .25, 0},  { delay + 5, 0}};
			} 
			
			// stop engine
			else if (_ignition < ignition) {
				starter.release(2);
			}

			ignition = _ignition;
    		rpm = max(0.0, _rpm / 900); // 900rpm = 1.0 (idle)
			rpm -= 0.05 * (rpm * rpm);  // slightly non-linear revs (energy loss at high revs)
    		throttle = _throttle;
			gear = _gear;				// (not currently used)
		}
    
		// generate audio
		void process() {
			// starter envelope (settles on 1.0)
			power = starter;

			// skip processing when idle
			if (power == 0) {
				out = 0;
				return;
			}

			// boost audible rpm for pulling away and accelerating
			signal new_rate = (rpm + rev + min(throttle, 0.707)) * power;
			if(rev.finished())
				throttle_lpf.set(0.5);
			
			// rev up is slower than rev down
			if(!rev.finished() || new_rate > rate)
				rate = rate * 0.9999 + 0.0001 * new_rate;
			else
				rate = rate * 0.999 + 0.001 * new_rate;

			// separate signal for overrev ('flooring it') 
			gas = max(0, ((throttle - 0.5) * 2)) >> throttle_lpf;
			gas *= 1 - min(0.75, abs(sqr(rate * 0.125)));
			const signal gas_2 = gas * gas;

			// engine noise 
			const signal n = noise;
			signal engine_noise = n * shelf;
			for (int i = 0; i < N; ++i)
				engine_noise += (n >> eq[i]) * eq_gain[i];
            
			// engine tone (resynthesised from Mini recording)
			for (int p = 0; p < 15; p++)
				osc[p].set(partials[p].frequency * rate * random(0.8, 1.2));
			signal engine_tone = 0;
			for (int p = 0; p < 15; ++p)
        		engine_tone += osc[p] * osc_gain[p];
        	
			// distort to emulate exhaust rasp
			engine_tone = softclip(engine_tone, 1.5, 1 + gas * gas * 0.25);
        
			// amplify exhaust for overrevs
			signal engine_throttle = (1 - gas) + gas * engine_tone;
			engine_throttle *= (0.5 + throttle * 0.5 + gas * gas * 0.1 * min(1, sqr(7.5 - rate) / 50.f + 0.125f));

			// modulate noise with engine tone
			signal am = engine_tone * engine_tone * engine_tone * engine_throttle * engine_noise;       

			// add slight resonance with comb filtering
			signal fb = comb(random(0.001, 0.002) * fs);
			am += gas * fb * 0.99 * max(0, (2 - rate));
			comb << am;

			// shape noise character based on revs with additional resonance for overreving
			lpf.set(5000 * (1.f + sqr(rate / 14.f)), max(1, 5 + gas_2 * 5/* - (rate - 1) * 1*/));
        
			// attenuate engine tone for higher revs
			param tone = max(0.5, 1 - abs(sqr(rate * 0.25 - 0.75))) * (1 - (rate - 1) * 0.01f);

			// mix together in proportion (based on revs and overrevs)
			engine_tone * tone + engine_noise * (rate * 0.02)  + (am >> lpf) * 0.075 * (1 + gas * gas) >> out;
			out *= power;
		}
	};

	Engine engine;

	// Initialise plugin (called once at startup)
	Mini() {
		controls = { 
			Toggle("Ignition"),
			Dial("RPM", 0, 7000, 000),
			Dial("Throttle", 0, 1, 0),
			Dial("Gear", -1, 5, 0)
		};
	}

	// Apply processing (called once per sample)
	void process() {
		param ignition = controls[0];
		param rpm = controls[1];
		param throttle = controls[2];
		param gear = controls[3];
			
		engine(ignition, rpm, throttle, gear) * 0.1 >> out;
	}
};

struct ToyBoatEngine : Generator {
	// signal generators
	Noise noise;
	Sine osc;
	
	// shaping
	BPF bp_9_15, bp_590_4;
	OnePole::HPF hip_10, hip_1000, hip_100;
	OnePole::LPF lop_30;
	
	// body resonances
	Bank<BPF, 3> body;
	
	bool brk = false;
	
	ToyBoatEngine() {
		osc.set(9);
		bp_9_15.set(9, 15);
		
		hip_10.set(10);
		lop_30.set(30);
		
		hip_1000.set(1000);
		bp_590_4.set(590, 4);
				
		body[0].set(470, 8);
		body[1].set(780, 9);
		body[2].set(1024, 10);
		
		hip_100.set(100);
	}
	virtual ~ToyBoatEngine() {}
	
	void set(param b){
		brk = b != 0;
	}
	
	void process() {
		signal mix = 0;
		
		if(brk) // engine broken>?
		 	noise >> bp_9_15 >> mix; // sputtering noise
		else
		 	osc >> mix; // regular pulse
		
		// exhaust outlet valve
		clip_0_1(mix * 600.0) >> hip_10 >> lop_30 >> mix;
		
		// formant filter (enveloped high-pass-filtered noise)
		mix *= noise >> hip_1000 >> bp_590_4;
		
		// tonal shaping (e.g. body resonances)
		mix >> body >> mix >> hip_100 >> out;
			
		// amplify output
		out *= 10;
	}
	
};

struct FourStrokeEngine : Generator {
	virtual ~FourStrokeEngine() {}

	param speed;
	
	Phasor phasor;
	Delay<3840> a, b;

	Noise noise;
	LPF lpf;
	BPF bpf;
	HPF hpf;

	DCF dc;
	
	void set(param spd){
		speed = spd;
		phasor.set(speed * 10);
		
		lpf.set(15);
		hpf.set(100);
		bpf.set(400, 0.5);
	}

	void process() {
		signal n = noise >> lpf;
		n * 30 >> b;
		n * 0.5 >> a;

		signal n2 = noise >> bpf(200 + speed * 400);// lop[1];
		n2 = 1.0 - (speed + 0.1) * n2 * 0.01;
		
		const signal i = phasor * 4;// *random(0.99, 1.0);;
		const signal s = 22 - speed * 15;
		const signal ms = fs / 250 *random(0.99, 1.0);
		
		out = 0;
		for(int d = 0; d < 4; d++){
			const param t = (d + 1) * 5 * ms;
			const param phase = -(0.75 - d * 0.25) * n2;
			
			signal mix = cos( (a(t) + i + phase) * 2 * pi );
			mix *= b(t) + s;
			
			out += 1 / (mix * mix + 1);
		}

		//out >> hpf 
		
		out * speed * min(speed,0.25) >> hpf >> dc >> out;
	}
};


struct Car : Sound {

	FourStrokeEngine engine;

	Car () {
		controls = { Dial("RPM", 0, 7000.f, 0.f) };
	}

	void prepare() override {
		param rpm = controls[0] / 7000.f;// max(0.075, controls[0] / 20000.f);
		engine.set(rpm);
	}

	void process() override {
		engine >> out;

		//const float normalise = 0.25f / tanh(10);
		0.25 * tanh(out * 3) / tanh(3) >> out;

		//out *= 10;
		//if (out > 1) out = 1;
		//else if (out < -1) out = -1;
		//out *= 1.0/10.0;
	}
};