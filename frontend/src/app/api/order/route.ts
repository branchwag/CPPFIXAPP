import { NextResponse } from 'next/server';

export async function POST(request: Request) {
	try {
		const body = await request.json();

		const response = await fetch('http://localhost:8080/api/order', {
			method: 'POST',
			headers: {
				'Content-Type': 'application/json',
			},
			body: JSON.stringify(body),
		});

		const data = await response.json();
		return NextResponse.json(data);
	} catch (error) {
		console.error('Error forwarding request:', error);
		return NextResponse.json(
			{ status: 'error', message: 'Failed to process order' },
			{ status: 500 }
		);
	}
}
